#include "nesmu.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PPUCTRL 0x2000
#define PPUMASK 0x2001
#define PPUSTATUS 0x2002
#define OAMADDR 0x2003
#define OAMDATA 0x2004
#define PPUSCROLL 0x2005
#define PPUADDR 0x2006
#define PPUDATA 0x2007
#define OAMDMA 0x4014

uint8_t ppu_read(t_nes *nes, uint16_t addr) {
    switch (addr) {
    case PPUSTATUS:
        nes->ppu_registers[addr & 7] &= ~128;
        nes->ppu_registers[addr & 7] |= (nes->NMI_occurred << 7);
        nes->NMI_occurred = false;
        return nes->ppu_registers[addr & 7];

    case PPUCTRL:
    case PPUMASK:
    case OAMADDR:
    case OAMDATA:
    case PPUSCROLL:
    case PPUADDR:
    case PPUDATA:
        return nes->ppu_registers[addr & 7];
    default:
        return 0;
    }
}

void ppu_write(t_nes *nes, uint16_t addr, uint8_t val) {
    switch (addr) {
    case PPUCTRL:
        nes->ppu_registers[addr & 7] = val;
        nes->NMI_output = (val & 128) ? true : false;
        break;
    case PPUMASK:
    case PPUSTATUS:
    case OAMADDR:
    case OAMDATA:
    case PPUSCROLL:
    case PPUADDR:
    case PPUDATA:
        nes->ppu_registers[addr & 7] = val;
        break;
    default:
    }
}

int ppu_get_x(t_nes *nes) { return nes->ppu_cycles % 341; }
int ppu_get_y(t_nes *nes) { return nes->ppu_cycles / 341; }

#define IS_VBLANK (nes->ppu_registers[2] & 128)
#define SET_VBLANK (nes->ppu_registers[2] |= 128)
#define CLEAR_VBLANK (nes->ppu_registers[2] &= ~128)
#define UPDATE_VBLANK(val) ((val) ? (SET_VBLANK) : (CLEAR_VBLANK))

int ppu_update(t_nes *nes) {
    uint32_t frame_durations[2] = {341 * 262, 341 * 261 + 340};
    uint32_t new_cpu_cycles, new_ppu_cycles, x, y;

    new_cpu_cycles = nes->cpu.cycles - nes->old_cpu_cycles;
    new_ppu_cycles = 3 * new_cpu_cycles;
    nes->ppu_cycles += new_ppu_cycles;
    if (frame_durations[nes->parity] <= nes->ppu_cycles) {
        nes->ppu_cycles %= frame_durations[nes->parity];
        nes->parity ^= 1;
        nes->frame_number += 1;
    }
    nes->old_cpu_cycles = nes->cpu.cycles;

    y = ppu_get_y(nes);
    x = ppu_get_x(nes);

    if ((!IS_VBLANK) && (241 == y) && (1 <= x)) {
        UPDATE_VBLANK(true);
        nes->NMI_occurred = true;
    }

    if ((IS_VBLANK) && (261 == y) && (1 <= x)) {
        UPDATE_VBLANK(false);
        nes->NMI_occurred = false;
    }

    nes->NMI_line_status_old = nes->NMI_line_status;
    nes->NMI_line_status = nes->NMI_occurred && nes->NMI_output;

    if (!nes->NMI_line_status_old && nes->NMI_line_status) {
        // check cpu IFLAG ?
        do_nmi(&(nes->cpu));
        nes->cpu.cycles += 7;
    }
    return 0;
}
