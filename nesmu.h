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

typedef struct nes {
    uint8_t memory[0x10000];
    t_cpu cpu;
    bool NMI_occurred, NMI_output;
    bool NMI_line_status, NMI_line_status_old;
    uint8_t ppu_registers[8];
    volatile uint32_t old_cpu_cycles, ppu_cycles, parity, frame_number;
} t_nes;

int run_opcode(t_nes *nes, bool debug);
void do_nmi(t_cpu *cpu);

int ppu_get_x(t_nes *);
int ppu_get_y(t_nes *);
uint8_t ppu_read(t_nes *, uint16_t);
void ppu_write(t_nes *, uint16_t, uint8_t);
int ppu_update(t_nes *);

#endif
