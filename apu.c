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

void apu_write(t_nes *nes, uint16_t addr, uint8_t val) {
    switch (addr) {
    case 4015:
    default:
        nes->memory[addr] = val;
        break;
    }
}

#define FRAME_COUNTER_MODE ((nes->memory[0x4017] & 128) ? 1 : 0)
#define IS_INTERRUPT_INHIBIT ((nes->memory[0x4017] & 64) ? 1 : 0)

static int quarter_frame_update(t_nes *nes) {
    // envelope and linear counter
    return 0;
}

static int half_frame_update(t_nes *nes) {
    // sweep and length counter
    return 0;
}

int16_t apu_collect_sample(t_nes *nes) { return 0; }

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

    /* (1_789_773 * 14700) / 44100 == 596591.0 */
    uint64_t output_cycles = 14700 * new_cpu_cycles;
    nes->apu.audio_output_cycles += output_cycles;
    while (nes->apu.audio_output_cycles >= 596591) {
        nes->apu.audio_output_cycles -= 596591;
        int16_t sample = apu_collect_sample(nes);
        audio_enqueue_sample(nes, sample);
    }
    return 0;
}
