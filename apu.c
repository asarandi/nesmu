#include "nesmu.h"
#include <stdio.h>

#define CH0 &(nes->apu.ch[0])
#define CH1 &(nes->apu.ch[1])
#define CH2 &(nes->apu.ch[2])
#define CH3 &(nes->apu.ch[3])
#define CH4 &(nes->apu.ch[4])

uint8_t apu_read(t_nes *nes, uint16_t addr) {
    uint8_t val = 0;

    switch (addr) {
    case SND_CHN:
        val |= (nes->apu.ch[0].lc.counter > 0) ? 1 : 0;
        val |= (nes->apu.ch[1].lc.counter > 0) ? 2 : 0;
        val |= (nes->apu.ch[2].lc.counter > 0) ? 4 : 0;
        val |= (nes->apu.ch[3].lc.counter > 0) ? 8 : 0;
        val |= (nes->apu.ch[4].dmc.sample_length > 0) ? 16 : 0;

        val |= nes->cpu.last_read & 32;
        val |= nes->apu.frame_interrupt_flag ? 64 : 0;
        val |= nes->apu.dmc_interrupt_flag ? 128 : 0;
        nes->apu.frame_interrupt_flag = false;
        return val;
    case JOY1:
        val = nes->cpu.last_read & 248;
        val |= (nes->shell.joy1 >> (7 - nes->joy1_read_index)) & 1;
        nes->joy1_read_index += 1;
        return val;
    case JOY2:
        return 0;

    default:
        return nes->cpu.last_read;
    }
}

static void quarter_frame_update(t_nes *nes);
static void half_frame_update(t_nes *nes);
static void dmc_reload(t_channel *, bool);
static void dmc_next_sample(t_nes *nes, t_channel *);

void apu_write(t_nes *nes, uint16_t addr, uint8_t val) {
    const uint8_t length_table[] = {
        10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
        12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
    const uint16_t noise_periods[] = {
        4,   8,   16,  32,  64,  96,   128,  160,
        202, 254, 380, 508, 762, 1016, 2034, 4068,
    };
    const uint16_t dmc_rates[] = {
        428, 380, 340, 320, 286, 254, 226, 214,
        190, 160, 142, 128, 106, 84,  72,  54,
    };

    t_channel *ch = NULL;

    ch = ((SQ1_VOL <= addr) && (addr < SQ2_VOL)) ? CH0 : ch;
    ch = ((SQ2_VOL <= addr) && (addr < TRI_LINEAR)) ? CH1 : ch;
    ch = ((TRI_LINEAR <= addr) && (addr < NOISE_VOL)) ? CH2 : ch;
    ch = ((NOISE_VOL <= addr) && (addr < DMC_FREQ)) ? CH3 : ch;
    ch = ((DMC_FREQ <= addr) && (addr < OAMDMA)) ? CH4 : ch;

    nes->memory[addr] = val;

    switch (addr) {
    case SQ1_VOL:
    case SQ2_VOL:
    case TRI_LINEAR:
    case NOISE_VOL:
    case DMC_FREQ:
        // pulse
        ch->env.loop_flag = (val & 32) != 0;
        ch->env.constant_volume = (val & 16) != 0;
        ch->env.period = val & 15;
        ch->env.duty = val >> 6;
        // triangle
        ch->lin.control_flag = (val & 128) != 0;
        ch->lin.period = val & 127;
        // dmc
        ch->dmc.irq_enabled_flag = (val & 128) != 0;
        if (!ch->dmc.irq_enabled_flag)
            nes->apu.dmc_interrupt_flag = false;
        ch->dmc.loop_flag = (val & 64) != 0;
        ch->dmc.period = dmc_rates[val & 15];
        break;

    case SQ1_SWEEP:
    case SQ2_SWEEP:
    case DMC_RAW:
        ch->sweep.reload_flag = true;
        ch->sweep.enabled = (val & 128) != 0;
        ch->sweep.negate_flag = (val & 8) != 0;
        ch->sweep.period = ((val >> 4) & 7) + 1;
        ch->sweep.shift = val & 7;
        // dmc
        ch->dmc.output = val & 127;
        break;

    case SQ1_LO:
    case SQ2_LO:
    case TRI_LO:
    case DMC_START:
        ch->timer.period = (ch->timer.period & 0xff00) | val;
        // dmc
        ch->dmc.start = val;
        break;

    case NOISE_LO:
        ch->timer.period = noise_periods[val & 15];
        ch->lfsr.mode_flag = (val & 128) != 0;
        break;

    case SQ1_HI:
    case SQ2_HI:
    case TRI_HI:
    case NOISE_HI:
    case DMC_LEN:
        ch->env.start_flag = true;

        if (addr != NOISE_HI) {
            ch->timer.period = ((val & 7) << 8) | (ch->timer.period & 255);
        }

        if (addr == SQ1_HI || addr == SQ2_HI) {
            ch->timer.phase = 0;
        }

        if (ch->lc.enabled) {
            ch->lc.counter = length_table[val >> 3];
        }

        // dmc
        ch->dmc.len = val;
        break;

    case SND_CHN:
        // When the enabled bit is cleared (via $4015), the length counter is
        // forced to 0 and cannot be changed until enabled is set again
        // (the length counter's previous value is lost).

        for (int i = 0; i < 4; i++) {
            bool enabled = (val & (1 << i)) != 0;
            nes->apu.ch[i].lc.enabled = enabled;
            if (!enabled) {
                nes->apu.ch[i].lc.counter = 0;
            }
        }

        // NB: `dmc_next_sample` might set flag, dmc-basics.nes test #19
        nes->apu.dmc_interrupt_flag = false;

        if (val & 16) {
            dmc_reload(CH4, true);
            dmc_next_sample(nes, CH4);
        } else {
            (CH4)->dmc.sample_length = 0;
        }
        break;

    case JOY1:
        nes->joy1_read_index = 0;
        break;

    case JOY2:
        nes->apu.cpu_cycles_divided = 0;
        if (val & 128) {
            quarter_frame_update(nes);
            half_frame_update(nes);
        }

        nes->apu.frame_counter_mode = (val & 128) != 0;
        nes->apu.interrupt_inhibit_flag = (val & 64) != 0;
        nes->apu.frame_interrupt_flag =
            (val & 64) ? false : nes->apu.frame_interrupt_flag;

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

// If the shift count is zero, the pulse channel's period is
// never updated but muting logic still applies.
static void sweep_tick(t_channel *ch, bool pulse_one) {
    uint16_t target, cond;

    target = ch->timer.period >> ch->sweep.shift;
    if (ch->sweep.negate_flag) {
        target = (pulse_one) ? ~target : -target;
    }
    target += ch->timer.period;

    ch->sweep.muted = (ch->timer.period < 8) || (target > 2047);

    cond = (ch->sweep.enabled) && (!ch->sweep.muted);
    cond &= (ch->sweep.shift) && (!ch->sweep.divider);
    ch->timer.period = cond ? target : ch->timer.period;

    if ((!ch->sweep.divider) || (ch->sweep.reload_flag)) {
        ch->sweep.divider = ch->sweep.period;
        ch->sweep.reload_flag = false;
    } else if (ch->sweep.divider) {
        ch->sweep.divider -= 1;
    }
}

static void length_counter_tick(t_channel *ch, bool disabled_flag) {
    if ((ch->lc.counter > 0) && (!disabled_flag)) {
        ch->lc.counter -= 1;
    }
}

static void pulse_timer_tick(t_channel *ch, bool phase_advance,
                             uint8_t phase_mask) {
    if (ch->timer.divider) {
        ch->timer.divider -= 1;
    } else {
        ch->timer.divider = ch->timer.period;
        if (phase_advance) {
            ch->timer.phase = (ch->timer.phase + 1) & phase_mask;
        }
    }
}

static void noise_timer_tick(t_channel *ch) {
    uint16_t reg, bit;

    if (ch->timer.divider) {
        ch->timer.divider -= 1;
    } else {
        ch->timer.divider = ch->timer.period;

        reg = ch->lfsr.shift_register;
        bit = ch->lfsr.mode_flag ? 6 : 1;
        bit = reg ^ (reg >> bit);
        ch->lfsr.shift_register = (reg >> 1) | ((bit & 1) << 14);
    }
}

static void dmc_reload(t_channel *ch, bool enabled) {
    ch->dmc.enabled = enabled;
    if (!ch->dmc.enabled) {
        ch->dmc.sample_length = 0;
        return;
    }

    if (ch->dmc.sample_length != 0)
        return;

    ch->dmc.sample_address = 0xc000 | (ch->dmc.start << 6);
    ch->dmc.sample_length = (ch->dmc.len << 4) + 1;
}

static void dmc_next_sample(t_nes *nes, t_channel *ch) {
    if ((ch->dmc.sample_length == 0) || (!ch->dmc.empty_buffer_flag))
        return;

    ch->dmc.empty_buffer_flag = false;
    // nes->cpu.dmc_halt_cycles += 4;

    ch->dmc.sample_buffer = nes->memory[ch->dmc.sample_address];
    ch->dmc.sample_address = (ch->dmc.sample_address + 1) | 0x8000;

    ch->dmc.sample_length -= 1;
    if (ch->dmc.sample_length == 0) {
        if (ch->dmc.loop_flag)
            dmc_reload(ch, ch->dmc.enabled);
        else if (ch->dmc.irq_enabled_flag)
            nes->apu.dmc_interrupt_flag = true;
    }
}

static void dmc_timer_tick(t_nes *nes, t_channel *ch) {
    if (ch->dmc.counter) {
        ch->dmc.counter -= 2; // cpu vs apu
        return;
    }
    ch->dmc.counter = ch->dmc.period;

    if (ch->dmc.empty_buffer_flag) {
        dmc_next_sample(nes, ch);
    }

    if (!ch->dmc.bits_remaining) {
        ch->dmc.bits_remaining = 8;
        if (ch->dmc.empty_buffer_flag) {
            ch->dmc.silence_flag = true;
        } else {
            ch->dmc.silence_flag = false;
            ch->dmc.shift_register = ch->dmc.sample_buffer;
            ch->dmc.empty_buffer_flag = true;
        }
    }

    if (!ch->dmc.silence_flag) {
        if (ch->dmc.shift_register & 1) {
            ch->dmc.output += (ch->dmc.output < 126) ? 2 : 0;
        } else {
            ch->dmc.output -= (ch->dmc.output > 1) ? 2 : 0;
        }
    }

    ch->dmc.shift_register >>= 1;
    ch->dmc.bits_remaining -= 1;
}

static uint8_t pulse_sample(t_channel *ch) {
    if ((ch->sweep.muted) || (!ch->lc.counter))
        return 0;

    uint8_t duties_table[] = {0b10000000, 0b11000000, 0b11110000, 0b00111111};
    uint8_t volume = ch->env.constant_volume ? ch->env.period : ch->env.decay;
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

static uint8_t triangle_sample(t_channel *ch) {
    uint8_t tab[] = {
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,  4,  3,  2,  1,  0,
        0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    };
    return tab[ch->timer.phase];
}

static uint8_t noise_sample(t_channel *ch) {
    if ((!ch->lc.counter) || (ch->lfsr.shift_register & 1))
        return 0;
    return ch->env.constant_volume ? ch->env.period : ch->env.decay;
}

static uint8_t dmc_sample(t_channel *ch) { return ch->dmc.output & 127; }

// frame sequencer

static void quarter_frame_update(t_nes *nes) {
    // envelope and linear counter
    envelope_tick(CH0);
    envelope_tick(CH1);
    linear_counter_tick(CH2);
    envelope_tick(CH3);
}

static void half_frame_update(t_nes *nes) {
    // sweep and length counter
    sweep_tick(CH0, true);
    sweep_tick(CH1, false);
    length_counter_tick(CH0, nes->apu.ch[0].env.loop_flag);
    length_counter_tick(CH1, nes->apu.ch[1].env.loop_flag);
    length_counter_tick(CH2, nes->apu.ch[2].lin.control_flag);
    length_counter_tick(CH3, nes->apu.ch[3].env.loop_flag);
}

static void apu_timers_tick(t_nes *nes) {
    // https://www.nesdev.org/wiki/APU#Glossary
    // The triangle channel's timer is clocked on every CPU cycle,
    // but the pulse, noise, and DMC timers are clocked only on
    // every second CPU cycle and thus produce only even periods.

    nes->apu.timer_cycles++;

    bool cond = (CH2)->lin.counter > 0;
    cond &= (CH2)->lc.counter > 0;
    // triangle: to avoid "ultrasonic frequencies"
    // do not change phase when period < 2
    cond &= (CH2)->timer.period > 1;
    pulse_timer_tick(CH2, cond, 31);

    if (nes->apu.timer_cycles < 2)
        return;
    nes->apu.timer_cycles -= 2;

    pulse_timer_tick(CH0, true, 7);
    pulse_timer_tick(CH1, true, 7);
    noise_timer_tick(CH3);
    dmc_timer_tick(nes, CH4);
}

static int16_t mix_samples(t_nes *nes) {
    // https://www.nesdev.org/wiki/APU_Mixer

    double calc, pulse_out, tnd_out, output;
    int16_t s0, s1, s2, s3, s4;
    int32_t out;

    s0 = s1 = s2 = s3 = s4 = 0;
    s0 = pulse_sample(CH0);
    s1 = pulse_sample(CH1);
    s2 = triangle_sample(CH2);
    s3 = noise_sample(CH3);
    s4 = dmc_sample(CH4);
    // s0 = s1 = s2 = s3 = s4 = 0;

    pulse_out = 0.0;
    if (s0 || s1) {
        pulse_out = 95.88 / (8128.0 / (double)(s0 + s1) + 100.0);
    }

    tnd_out = 0.0;
    if (s2 || s3 || s4) {
        calc = s2 / 8227.0 + s3 / 12241.0 + s4 / 22638.0;
        tnd_out = 159.79 / (1.0 / calc + 100.0);
    }

    calc = (pulse_out + tnd_out) * 2.0;
    output = calc - nes->apu.capacitor;
    nes->apu.capacitor = calc - output * 0.999929;
    out = (double)INT16_MAX * output;
    out = out > INT16_MAX ? INT16_MAX : out;
    out = out < INT16_MIN ? INT16_MIN : out;
    return (int16_t)out;
}

static void apu_tick(t_nes *nes) {

    // 2 cpu cycles == 1 apu cycles
    // cpu rate: 1,789,773 Hz per second
    uint32_t i, j;
    uint32_t sequencer_steps[2][4] = {
        {7457, 14913, 22371, 29830},
        {7457, 14913, 22371, 37282},
    };
    i = (int)nes->apu.frame_counter_mode;

    nes->apu.cpu_cycles += 1;
    nes->apu.cpu_cycles_divided += 1;

    j = nes->apu.cpu_cycles_divided;

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

        nes->apu.cpu_cycles_divided -= sequencer_steps[i][3];
        if ((i == 0) && (!nes->apu.interrupt_inhibit_flag)) {
            nes->apu.frame_interrupt_flag = true;
        }
    } else {
    }

    if ((nes->apu.frame_interrupt_flag) || (nes->apu.dmc_interrupt_flag)) {
        int new_cycles = do_irq(&(nes->cpu));
        nes->cpu.cycles += new_cycles;
        if (new_cycles) {
            puts("APU MODE 0 IRQ");
        }
    }

    apu_timers_tick(nes);

    int16_t sample = mix_samples(nes);

    /* (1_789_773 * 16000) / 48000 == 596591.0 */
    nes->apu.audio_output_cycles += 16000;
    while (nes->apu.audio_output_cycles >= 596591) {
        nes->apu.audio_output_cycles -= 596591;
        audio_enqueue_sample(nes, sample);
    }
}

int apu_update(t_nes *nes) {
    uint32_t new_cpu_cycles = nes->cpu.cycles - nes->prev_cpu_cycles;
    while (new_cpu_cycles--) {
        apu_tick(nes);
    }
    return 0;
}
