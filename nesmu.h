#ifndef NESMU_H
#define NESMU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct cpu {
    uint8_t A, X, Y, S, P, u8, last_read;
    uint16_t PC, u16;
    void *userdata;
    uint8_t (*read)(void *userdata, uint16_t addr);
    void (*write)(void *userdata, uint16_t addr, uint8_t val);
    volatile uint32_t cycles;
    volatile uint8_t extra_cycles;
} t_cpu;

typedef struct apu {
    volatile uint32_t cpu_cycles, step_index, prev_step_index;
    volatile uint64_t audio_output_cycles;
} t_apu;

typedef struct shell {
    int audio_device;
    int16_t buf[512 * 2];
    volatile int buf_read_index, buf_write_index, num_available;
} t_shell;

typedef struct nes {
    uint8_t memory[0x10000];
    t_cpu cpu;
    t_apu apu;
    t_shell shell;
    bool NMI_occurred, NMI_output;
    bool NMI_line_status, NMI_line_status_old;
    uint8_t ppu_registers[8];
    volatile uint32_t prev_cpu_cycles, ppu_cycles, parity, frame_number;
} t_nes;

bool cpu_is_iflag(t_nes *);
int run_opcode(t_nes *, bool);
void do_nmi(t_cpu *);

int ppu_get_x(t_nes *);
int ppu_get_y(t_nes *);

uint8_t ppu_read(t_nes *, uint16_t);
void ppu_write(t_nes *, uint16_t, uint8_t);
int ppu_update(t_nes *);

uint8_t apu_read(t_nes *, uint16_t);
void apu_write(t_nes *, uint16_t, uint8_t);
int apu_update(t_nes *);

int shell_open(t_nes *);
int shell_close(t_nes *);
int poll_events(t_nes *, int *);
void audio_enqueue_sample(t_nes *, int16_t);

#endif
