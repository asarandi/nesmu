#ifndef NESMU_H
#define NESMU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct cpu {
    uint8_t A, X, Y, S, P, u8;
    uint16_t PC, u16;
    void *userdata;
    uint8_t (*read)(void *userdata, uint16_t addr);
    void (*write)(void *userdata, uint16_t addr, uint8_t val);
    uint32_t cycles;
    uint8_t extra_cycles;
} t_cpu;

int run_opcode(t_cpu *cpu, bool debug);

int ppu_get_x();
int ppu_get_y();
uint8_t ppu_read(void *, uint16_t);
void ppu_write(void *, uint16_t, uint8_t);
int ppu_update(uint32_t);

#endif
