#include "nesmu.h"
#include <assert.h>
#include <stdio.h>

uint8_t apu_read(t_nes *nes, uint16_t addr) {
    t_pulse *ch1 = &(nes->apu.pulse1);
    t_pulse *ch2 = &(nes->apu.pulse2);
    switch (addr) {
    case SND_CHN:
        uint8_t val = 0;
        val |= (ch1->lenctr != 0) << 0;
        val |= (ch2->lenctr != 0) << 1;
        return val;
    case JOY1:
        return 0;
    case JOY2:
        return 0;

    default:
        return nes->cpu.last_read;
    }
}

static uint16_t apu_pulse_timer_value(t_nes *nes, bool one) {
    return one ? (((nes->memory[SQ1_HI] & 7) << 8) | nes->memory[SQ1_LO])
               : (((nes->memory[SQ2_HI] & 7) << 8) | nes->memory[SQ2_LO]);
}

static int quarter_frame_update(t_nes *nes);
static int half_frame_update(t_nes *nes);

void apu_write(t_nes *nes, uint16_t addr, uint8_t val) {
    const uint8_t length_table[] = {
        10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
        12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

    t_pulse *ch1 = &(nes->apu.pulse1);
    t_pulse *ch2 = &(nes->apu.pulse2);

    t_pulse *ch;
    if ((SQ1_VOL <= addr) && (addr < SQ2_VOL))
        ch = ch1;
    else if ((SQ2_VOL <= addr) && (addr < TRI_LINEAR))
        ch = ch2;
    else
        ch = NULL;

    switch (addr) {
    case SQ1_VOL:
    case SQ2_VOL:
        nes->memory[addr] = val;
        ch->env_loop = (val & 32) != 0;
        ch->const_vol = (val & 16) != 0;
        ch->env_period = val & 15;
        ch->duty = val >> 6;
        break;

    case SQ1_HI:
    case SQ2_HI:
        nes->memory[addr] = val;
        ch->env_start = 1;
        ch->seq = 0;
        if (ch->lc_enabled) {
            ch->lenctr = length_table[val >> 3];
        }
        break;

    case SND_CHN:
        // When the enabled bit is cleared (via $4015), the length counter is
        // forced to 0 and cannot be changed until enabled is set again
        // (the length counter's previous value is lost).

        nes->memory[addr] = val;
        ch1->lc_enabled = (val & 1) != 0;
        ch1->lenctr = ch1->lc_enabled ? ch1->lenctr : 0;
        ch2->lc_enabled = (val & 2) != 0;
        ch2->lenctr = ch2->lc_enabled ? ch2->lenctr : 0;
        break;

    case JOY2:
        nes->memory[addr] = val;
        nes->apu.cpu_cycles = 0;
        if (val & 128) {
            quarter_frame_update(nes);
            half_frame_update(nes);
        }
        break;

    default:
        nes->memory[addr] = val;
        break;
    }
}

static int apu_envelope_update(t_nes *nes, bool one) {
    t_pulse *ch = one ? &(nes->apu.pulse1) : &(nes->apu.pulse2);

    if (!ch->env_start) {
        if (ch->env_divider) {
            ch->env_divider -= 1;
        } else {
            ch->env_divider = ch->env_period;
            if (ch->env_decay) {
                ch->env_decay -= 1;
            } else if (ch->env_loop) {
                ch->env_decay = 15;
            }
        }
    } else {
        ch->env_start = 0;
        ch->env_decay = 15;
        ch->env_divider = ch->env_period;
    }

    return 0;
}

static int quarter_frame_update(t_nes *nes) {
    // envelope and linear counter
    apu_envelope_update(nes, true);
    apu_envelope_update(nes, false);
    return 0;
}

static int apu_pulse_length_counter_tick(t_nes *nes, bool one) {
    t_pulse *ch = one ? &(nes->apu.pulse1) : &(nes->apu.pulse2);
    if (ch->env_loop)
        return 1;
    ch->lenctr -= (ch->lenctr > 0) ? 1 : 0;
    return 0;
}

static int half_frame_update(t_nes *nes) {
    // sweep and length counter
    apu_pulse_length_counter_tick(nes, true);
    apu_pulse_length_counter_tick(nes, false);
    return 0;
}

static int apu_pulse_timer_tick(t_nes *nes, bool one) {
    t_pulse *ch = one ? &(nes->apu.pulse1) : &(nes->apu.pulse2);
    uint16_t timer = apu_pulse_timer_value(nes, one);

    if (ch->timer) {
        ch->timer -= 1;
    } else {
        ch->timer = apu_pulse_timer_value(nes, one);
        ch->seq = (ch->seq + 1) & 7;
    }
    return 0;
}

static int apu_timers_tick(t_nes *nes) {
    // https://www.nesdev.org/wiki/APU#Glossary
    // The triangle channel's timer is clocked on every CPU cycle,
    // but the pulse, noise, and DMC timers are clocked only on
    // every second CPU cycle and thus produce only even periods.

    nes->apu.timer_cycles++;
    if (nes->apu.timer_cycles > 1) {
        nes->apu.timer_cycles -= 2;
        apu_pulse_timer_tick(nes, true);
        apu_pulse_timer_tick(nes, false);
    }
    // tick triangle, noise
    return 0;
}

static int apu_pulse_sample(t_nes *nes, bool one) {
    t_pulse *ch = one ? &(nes->apu.pulse1) : &(nes->apu.pulse2);
    uint16_t timer = apu_pulse_timer_value(nes, one);
    int volume = ch->const_vol ? ch->env_period : ch->env_decay;
    const uint8_t duties_table[] = {0b10000000, 0b11000000, 0b11110000,
                                    0b00111111};
    if ((timer < 8) || (!ch->lenctr))
        return 0;

    uint8_t pat = duties_table[ch->duty];
    uint8_t hi = pat & (1 << (7 - ch->seq));
    return hi ? volume : -volume;
}

int16_t apu_collect_sample(t_nes *nes) {
    // mixer

    int16_t b, s1, s2;

    s1 = s2 = 0;
    s1 = apu_pulse_sample(nes, true);
    s2 = apu_pulse_sample(nes, false);

    int32_t sum = s1 + s2;
    return sum * (INT16_MAX / 64);
}

#define FRAME_COUNTER_MODE ((nes->memory[JOY2] & 128) ? 1 : 0)
#define IS_INTERRUPT_INHIBIT ((nes->memory[JOY2] & 64) ? 1 : 0)

int apu_tick(t_nes *nes) {

    // 2 cpu cycles == 1 apu cycles
    // cpu rate: 1,789,773 Hz per second
    uint32_t i, j;
    uint32_t sequencer_steps[2][4] = {
        {7457, 14913, 22371, 29830},
        {7457, 14913, 22371, 37282},
    };
    i = FRAME_COUNTER_MODE;

    nes->apu.cpu_cycles += 1;
    j = nes->apu.cpu_cycles;

    if (j == sequencer_steps[i][0]) {
        quarter_frame_update(nes);
    } else if (j == sequencer_steps[i][1]) {
        quarter_frame_update(nes);
        half_frame_update(nes);
    } else if (j == sequencer_steps[i][2]) {
        quarter_frame_update(nes);
    } else if (j >= sequencer_steps[i][3]) {
        nes->apu.cpu_cycles -= sequencer_steps[i][3];
        quarter_frame_update(nes);
        half_frame_update(nes);
        if ((i == 0) && (!IS_INTERRUPT_INHIBIT) && (!cpu_is_iflag(nes))) {
            puts("APU MODE 0 IRQ");
        }
    } else {
    }

    apu_timers_tick(nes);

    /* (1_789_773 * 16000) / 48000 == 596591.0 */
    nes->apu.audio_output_cycles += 16000;
    while (nes->apu.audio_output_cycles >= 596591) {
        nes->apu.audio_output_cycles -= 596591;
        int16_t sample = apu_collect_sample(nes);
        audio_enqueue_sample(nes, sample);
    }
    return 0;
}

int apu_update(t_nes *nes) {
    uint32_t new_cpu_cycles = nes->cpu.cycles - nes->prev_cpu_cycles;
    while (new_cpu_cycles--) {
        apu_tick(nes);
    }
    return 0;
}
