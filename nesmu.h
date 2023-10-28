#ifndef NESMU_H
# define NESMU_H

#include <stdint.h>

typedef struct cpu {
    uint8_t A, X, Y, S, P, u8;
    uint16_t PC, u16;
    void *userdata;
    uint8_t (*read)(void *userdata, uint16_t addr);
    void (*write)(void *userdata, uint16_t addr, uint8_t val);
    uint32_t cycles;
} t_cpu;

int run_opcode(t_cpu *cpu);

#endif
