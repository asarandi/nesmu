#include "nesmu.h"
#include <stdio.h>

#define CH0 &(nes->apu.ch[0])
#define CH1 &(nes->apu.ch[1])
#define CH2 &(nes->apu.ch[2])
#define CH3 &(nes->apu.ch[3])
#define CH4 &(nes->apu.ch[4])

uint8_t apu_read(t_nes *nes, uint16_t addr) {
    switch (addr) {
    case SND_CHN:
        uint8_t val = 0;
        for (int i = 0; i < 5; i++) {
            val |= (nes->apu.ch[i].lc.counter > 0) << i;
        }
        return val;
    case JOY1:
        return 0;
    case JOY2:
        return 0;

    default:
        return nes->cpu.last_read;
    }
}

static int quarter_frame_update(t_nes *nes);
static int half_frame_update(t_nes *nes);

void apu_write(t_nes *nes, uint16_t addr, uint8_t val) {
    const uint8_t length_table[] = {
        10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
        12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

    t_channel *ch = NULL;

    ch = ((SQ1_VOL <= addr) && (addr < SQ2_VOL)) ? CH0 : ch;
    ch = ((SQ2_VOL <= addr) && (addr < TRI_LINEAR)) ? CH1 : ch;
    ch = ((TRI_LINEAR <= addr) && (addr < NOISE_VOL)) ? CH2 : ch;

    nes->memory[addr] = val;

    switch (addr) {
    case SQ1_VOL:
    case SQ2_VOL:
    case TRI_LINEAR:
        // pulse
        ch->env.loop_flag = (val & 32) != 0;
        ch->env.constant_volume = (val & 16) != 0;
        ch->env.period = val & 15;
        ch->env.duty = val >> 6;
        // triangle
        ch->lin.control_flag = (val & 128) != 0;
        ch->lin.period = val & 127;
        break;

    case SQ1_LO:
    case SQ2_LO:
    case TRI_LO:
        ch->timer.period = (ch->timer.period & 0xff00) | val;
        break;

    case SQ1_HI:
    case SQ2_HI:
    case TRI_HI:
        ch->env.start_flag = true;
        ch->timer.period = ((val & 7) << 8) | (ch->timer.period & 255);

        if (addr == SQ1_HI || addr == SQ2_HI) {
            ch->timer.phase = 0;
        }

        if (ch->lc.enabled) {
            ch->lc.counter = length_table[val >> 3];
        }

        break;

    case SND_CHN:
        // When the enabled bit is cleared (via $4015), the length counter is
        // forced to 0 and cannot be changed until enabled is set again
        // (the length counter's previous value is lost).

        for (int i = 0; i < 5; i++) {
            bool enabled = (val & (1 << i)) != 0;
            nes->apu.ch[i].lc.enabled = enabled;
            if (!enabled) {
                nes->apu.ch[i].lc.counter = 0;
            }
        }
        break;

    case JOY2:
        nes->apu.cpu_cycles = 0;
        if (val & 128) {
            quarter_frame_update(nes);
            half_frame_update(nes);
        }
        break;

    default:
        break;
    }
}

static void envelope_tick(t_channel *ch) {
    if (ch->env.start_flag) {
        ch->env.start_flag = false;
        ch->env.decay = 15;
        ch->env.divider = ch->env.period;
    } else if (ch->env.divider) {
        ch->env.divider -= 1;
    } else {
        ch->env.divider = ch->env.period;
        if (ch->env.decay) {
            ch->env.decay -= 1;
        } else if (ch->env.loop_flag) {
            ch->env.decay = 15;
        }
    }
}

static void length_counter_tick(t_channel *ch, bool disabled_flag) {
    if ((ch->lc.counter > 0) && (!disabled_flag)) {
        ch->lc.counter -= 1;
    }
}

static void timer_tick(t_channel *ch, bool phase_advance, uint8_t phase_mask) {
    if (ch->timer.divider) {
        ch->timer.divider -= 1;
    } else {
        ch->timer.divider = ch->timer.period;
        if (phase_advance) {
            ch->timer.phase = (ch->timer.phase + 1) & phase_mask;
        }
    }
}

static int pulse_sample(t_channel *ch) {
    uint16_t timer = ch->timer.period;
    int volume = ch->env.constant_volume ? ch->env.period : ch->env.decay;
    const uint8_t duties_table[] = {0b10000000, 0b11000000, 0b11110000,
                                    0b00111111};
    if ((timer < 8) || (!ch->lc.counter))
        return 0;

    uint8_t pat = duties_table[ch->env.duty];
    uint8_t hi = pat & (1 << (7 - ch->timer.phase));
    return hi ? volume : 0;
}

// triangle

static void linear_counter_tick(t_channel *ch) {
    if (ch->env.start_flag) {
        ch->lin.counter = ch->lin.period;
    } else if (ch->lin.counter > 0) {
        ch->lin.counter -= 1;
    }
    if (!ch->lin.control_flag) {
        ch->env.start_flag = false;
    }
}

static int triangle_sample(t_channel *ch) {
    uint8_t tab[] = {
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,  4,  3,  2,  1,  0,
        0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    };
    return tab[ch->timer.phase];
}

// frame sequencer

static int quarter_frame_update(t_nes *nes) {
    // envelope and linear counter
    envelope_tick(CH0);
    envelope_tick(CH1);
    linear_counter_tick(CH2);
    return 0;
}

static int half_frame_update(t_nes *nes) {
    // sweep and length counter
    length_counter_tick(CH0, nes->apu.ch[0].env.loop_flag);
    length_counter_tick(CH1, nes->apu.ch[1].env.loop_flag);
    length_counter_tick(CH2, nes->apu.ch[2].lin.control_flag);
    return 0;
}

static int timers_tick(t_nes *nes) {
    // https://www.nesdev.org/wiki/APU#Glossary
    // The triangle channel's timer is clocked on every CPU cycle,
    // but the pulse, noise, and DMC timers are clocked only on
    // every second CPU cycle and thus produce only even periods.

    nes->apu.timer_cycles++;
    if (nes->apu.timer_cycles > 1) {
        nes->apu.timer_cycles -= 2;
        timer_tick(CH0, true, 7);
        timer_tick(CH1, true, 7);
    }

    timer_tick(CH2, (CH2)->lin.counter > 0 && (CH2)->lc.counter > 0, 31);

    // tick noise
    return 0;
}

static int16_t mix_samples(t_nes *nes) {
    // https://www.nesdev.org/wiki/APU_Mixer

    int16_t s1, s2, s3, retval;
    double pulse_out, tnd_out, output;

    s1 = s2 = s3 = 0;
    s1 = pulse_sample(CH0);
    s2 = pulse_sample(CH1);
    s3 = triangle_sample(CH2);

    pulse_out = 0.0;
    if (s1 || s2) {
        pulse_out = 95.88 / (8128.0 / (double)(s1 + s2) + 100.0);
    }

    tnd_out = 0.0;
    if (s3) {
        tnd_out = 159.79 / (1.0 / ((double)s3 / 8227.0) + 100.0);
    }

    output = pulse_out + tnd_out;

    retval = (int16_t)((double)INT16_MAX * output);
    return retval;
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
        quarter_frame_update(nes);
        half_frame_update(nes);

        nes->apu.cpu_cycles -= sequencer_steps[i][3];
        if ((i == 0) && (!IS_INTERRUPT_INHIBIT) && (!cpu_is_iflag(nes))) {
            puts("APU MODE 0 IRQ");
        }
    } else {
    }

    timers_tick(nes);

    /* (1_789_773 * 16000) / 48000 == 596591.0 */
    nes->apu.audio_output_cycles += 16000;
    while (nes->apu.audio_output_cycles >= 596591) {
        nes->apu.audio_output_cycles -= 596591;
        int16_t sample = mix_samples(nes);
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
