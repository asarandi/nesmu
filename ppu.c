#include <stdbool.h>
#include <stdint.h>

#define PPUCTRL 0x2000
#define PPUMASK 0x2001
#define PPUSTATUS 0x2002
#define OAMADDR 0x2003
#define OAMDATA 0x2004
#define PPUSCROLL 0x2005
#define PPUADDR 0x2006
#define PPUDATA 0x2007
#define OAMDMA 0x4014

static uint8_t ppu_registers[8] = {0, 0, 0, 0, 0, 0, 0, 0};

uint8_t ppu_read(void *userdata, uint16_t addr) {
    (void)userdata;
    switch (addr) {
    case PPUCTRL:
    case PPUMASK:
    case PPUSTATUS:
    case OAMADDR:
    case OAMDATA:
    case PPUSCROLL:
    case PPUADDR:
    case PPUDATA:
        return ppu_registers[addr & 7];
    default:
        return 0;
    }
}

void ppu_write(void *userdata, uint16_t addr, uint8_t val) {
    (void)userdata;
    switch (addr) {
    case PPUCTRL:
    case PPUMASK:
    case PPUSTATUS:
    case OAMADDR:
    case OAMDATA:
    case PPUSCROLL:
    case PPUADDR:
    case PPUDATA:
        ppu_registers[addr & 7] = val;
        break;
    default:
    }
}

volatile uint32_t old_cpu_cycles, ppu_cycles, parity, frame_number;

int ppu_get_x() { return ppu_cycles % 341; }
int ppu_get_y() { return ppu_cycles / 341; }

#define IS_VBLANK (ppu_registers[2] & 128)
#define SET_VBLANK (ppu_registers[2] |= 128)
#define CLEAR_VBLANK (ppu_registers[2] &= ~128)
#define UPDATE_VBLANK(val) ((val) ? (SET_VBLANK) : (CLEAR_VBLANK))

int ppu_update(uint32_t cpu_cycles) {
    uint32_t frame_durations[2] = {341 * 262, 341 * 261 + 340};
    uint32_t new_cpu_cycles, new_ppu_cycles, x, y;

    new_cpu_cycles = cpu_cycles - old_cpu_cycles;
    new_ppu_cycles = 3 * new_cpu_cycles;
    ppu_cycles += new_ppu_cycles;
    if (frame_durations[parity] <= ppu_cycles) {
        ppu_cycles %= frame_durations[parity];
        parity ^= 1;
        frame_number += 1;
    }

    y = ppu_get_y();
    x = ppu_get_x();

    if ((!IS_VBLANK) && (241 == y) && (1 <= x))
        UPDATE_VBLANK(true);
    if ((IS_VBLANK) && (261 == y) && (1 <= x))
        UPDATE_VBLANK(false);

    old_cpu_cycles = cpu_cycles;
    return 0;
}
