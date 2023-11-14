#include "nesmu.h"
#include <stdio.h>

uint8_t apu_read(t_nes *nes, uint16_t addr) {
    switch (addr) {
    case 4015:
        return nes->memory[addr];
    default:
        return nes->cpu.last_read;
    }
}

static uint16_t apu_pulse_timer_value(t_nes *nes, uint16_t hi, uint16_t lo) {
    return ((nes->memory[hi] & 7) << 8) | nes->memory[lo];
}

void apu_write(t_nes *nes, uint16_t addr, uint8_t val) {
    const uint8_t length_table[] = {
        10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
        12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

    t_pulse *ch1 = &(nes->apu.pulse1);
    t_pulse *ch2 = &(nes->apu.pulse2);

    switch (addr) {
    case SQ1_HI:
        nes->memory[addr] = val;
        //        ch1->timer = apu_pulse_timer_value(nes, SQ1_HI, SQ1_LO);
        ch1->seq = 0;
        ch1->env_start = 1;
        ch1->lenctr = length_table[val >> 3];
        break;

    case SQ2_HI:
        nes->memory[addr] = val;
        //        ch2->timer = apu_pulse_timer_value(nes, SQ2_HI, SQ2_LO);
        ch2->seq = 0;
        ch2->env_start = 1;
        ch2->lenctr = length_table[val >> 3];
        break;

    case SND_CHN:
        // When the enabled bit is cleared (via $4015), the length counter is
        // forced to 0 and cannot be changed until enabled is set again
        // (the length counter's previous value is lost).

        nes->memory[addr] = val;
        ch1->lenctr = (val & 1) ? ch1->lenctr : 0;
        ch2->lenctr = (val & 2) ? ch2->lenctr : 0;
        break;

    default:
        nes->memory[addr] = val;
        break;
    }
}

#define FRAME_COUNTER_MODE ((nes->memory[JOY2] & 128) ? 1 : 0)
#define IS_INTERRUPT_INHIBIT ((nes->memory[JOY2] & 64) ? 1 : 0)

static int apu_envelope_update(t_nes *nes, bool ch1) {
    t_pulse *ch = ch1 ? &(nes->apu.pulse1) : &(nes->apu.pulse2);
    uint16_t reg_vol = ch1 ? SQ1_VOL : SQ2_VOL;

    if (!ch->env_start) {
        if (ch->env_divider) {
            ch->env_divider -= 1;
        } else {
            ch->env_divider = nes->memory[reg_vol] & 15;
            if (ch->env_decay) {
                ch->env_decay -= 1;
            } else {
                if (nes->memory[reg_vol] & 32) {
                    ch->env_decay = 15;
                }
            }
        }
    } else {
        ch->env_start = 0;
        ch->env_decay = 15;
        ch->env_divider = nes->memory[reg_vol] & 15;
    }

    return 0;
}

static int quarter_frame_update(t_nes *nes) {
    // envelope and linear counter
    apu_envelope_update(nes, true);
    apu_envelope_update(nes, false);
    return 0;
}

static int half_frame_update(t_nes *nes) {
    // sweep and length counter
    t_pulse *ch1 = &(nes->apu.pulse1);
    t_pulse *ch2 = &(nes->apu.pulse2);

    if (!(nes->memory[SQ1_VOL] & 32)) {
        if (ch1->lenctr)
            ch1->lenctr -= 1;
    }

    if (!(nes->memory[SQ2_VOL] & 32)) {
        if (ch2->lenctr)
            ch2->lenctr -= 1;
    }

    return 0;
}

static int apu_update_timers(t_nes *nes, uint32_t new_cpu_cycles) {
    // https://www.nesdev.org/wiki/APU#Glossary
    // The triangle channel's timer is clocked on every CPU cycle,
    // but the pulse, noise, and DMC timers are clocked only on
    // every second CPU cycle and thus produce only even periods.

    t_pulse *ch1 = &(nes->apu.pulse1);
    t_pulse *ch2 = &(nes->apu.pulse2);

    uint32_t timer_cycles = nes->apu.timer_cycles + new_cpu_cycles;
    nes->apu.timer_cycles = timer_cycles & 1;
    timer_cycles >>= 1;

    while (timer_cycles--) {
        if (ch1->timer) {
            ch1->timer -= 1;
        } else {
            ch1->timer = apu_pulse_timer_value(nes, SQ1_HI, SQ1_LO);
            ch1->seq = (ch1->seq + 1) & 7;
        }

        if (ch2->timer) {
            ch2->timer -= 1;
        } else {
            ch2->timer = apu_pulse_timer_value(nes, SQ2_HI, SQ2_LO);
            ch2->seq = (ch2->seq + 1) & 7;
        }
    }

    return 0;
}

static uint8_t apu_pulse_sample(t_nes *nes, bool ch1) {
    t_pulse *ch = ch1 ? &(nes->apu.pulse1) : &(nes->apu.pulse2);
    uint16_t timer = ch1 ? apu_pulse_timer_value(nes, SQ1_HI, SQ1_LO)
                         : apu_pulse_timer_value(nes, SQ2_HI, SQ2_LO);
    if ((timer < 8) || (!ch->lenctr))
        return 0;

    uint8_t *p = nes->memory;
    uint16_t reg_vol = ch1 ? SQ1_VOL : SQ2_VOL;
    uint8_t index = p[reg_vol] >> 6;
    uint8_t patterns[] = {0b01000000, 0b01100000, 0b01111000, 0b10011111};
    uint8_t duty = patterns[index];
    uint8_t hi = duty & (1 << (7 - ch->seq));
    uint8_t volume = (p[reg_vol] & 16) ? (p[reg_vol] & 15) : ch->env_decay;
    return hi ? volume : 0;
}

int16_t apu_collect_sample(t_nes *nes) {
    // mixer

    uint8_t b, s1, s2;

    b = nes->memory[SND_CHN];

    s1 = b & 1 ? apu_pulse_sample(nes, true) : 0;
    s2 = b & 2 ? apu_pulse_sample(nes, false) : 0;

    int32_t sum = s1 + s2;

    return sum * (INT16_MAX / 30);
}

int apu_update(t_nes *nes) {

    // 2 cpu cycles == 1 apu cycles
    // cpu rate: 1,789,773 Hz per second
    uint32_t i, j, k,
        sequencer_steps[2][4] = {
            {7457, 14913, 22371, 29829},
            {7457, 14913, 22371, 37281},
        };
    i = FRAME_COUNTER_MODE;

    uint32_t new_cpu_cycles = nes->cpu.cycles - nes->prev_cpu_cycles;
    nes->apu.cpu_cycles += new_cpu_cycles;
    apu_update_timers(nes, new_cpu_cycles);

    nes->apu.cpu_cycles %= sequencer_steps[i][3];
    for (j = 0; j < 4; j++) {
        if (nes->apu.cpu_cycles < sequencer_steps[i][j]) {
            break;
        }
    }
    nes->apu.prev_step_index = k = nes->apu.step_index;
    nes->apu.step_index = j;

    if ((j == 0) && (k == 3)) {
        quarter_frame_update(nes);
    } else if ((j == 1) && (k == 0)) {
        quarter_frame_update(nes);
        half_frame_update(nes);
    } else if ((j == 2) && (k == 1)) {
        quarter_frame_update(nes);
    } else if ((j == 3) && (k == 2)) {
        quarter_frame_update(nes);
        half_frame_update(nes);
        if ((i == 0) && (!IS_INTERRUPT_INHIBIT) && (!cpu_is_iflag(nes))) {
            puts("APU MODE 0 IRQ");
        }
    } else {
    }

    /* (1_789_773 * 16000) / 48000 == 596591.0 */
    uint64_t output_cycles = 16000 * new_cpu_cycles;
    nes->apu.audio_output_cycles += output_cycles;
    while (nes->apu.audio_output_cycles >= 596591) {
        nes->apu.audio_output_cycles -= 596591;
        int16_t sample = apu_collect_sample(nes);
        audio_enqueue_sample(nes, sample);
    }
    return 0;
}
