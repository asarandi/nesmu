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

int apu_update(t_nes *nes) {

    // 2 cpu cycles == 1 apu cycles
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

    if ((i == 0) && (j == 3) && (k == 2)) {
        if ((!IS_INTERRUPT_INHIBIT) && (!cpu_is_iflag(nes))) {
            // do irq
            puts("APU MODE 0 IRQ");
        }
    }
    if ((i == 1) && (j == 3) && (k == 2)) {
        // do irq
        puts("APU MODE 1 NO IRQ");
    }

    return 0;
}
