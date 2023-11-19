#ifndef NESMU_H
#define NESMU_H

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
#define SQ1_VOL 0x4000
#define SQ1_SWEEP 0x4001
#define SQ1_LO 0x4002
#define SQ1_HI 0x4003
#define SQ2_VOL 0x4004
#define SQ2_SWEEP 0x4005
#define SQ2_LO 0x4006
#define SQ2_HI 0x4007
#define TRI_LINEAR 0x4008
#define TRI_LO 0x400A
#define TRI_HI 0x400B
#define NOISE_VOL 0x400C
#define NOISE_LO 0x400E
#define NOISE_HI 0x400F
#define DMC_FREQ 0x4010
#define DMC_RAW 0x4011
#define DMC_START 0x4012
#define DMC_LEN 0x4013
#define OAMDMA 0x4014
#define SND_CHN 0x4015
#define JOY1 0x4016
#define JOY2 0x4017

typedef struct cpu {
    uint8_t A, X, Y, S, P, u8, last_read;
    uint16_t PC, u16;
    void *userdata;
    uint8_t (*read)(void *userdata, uint16_t addr);
    void (*write)(void *userdata, uint16_t addr, uint8_t val);
    volatile uint32_t cycles;
    volatile uint8_t extra_cycles;
} t_cpu;

typedef struct envelope {
    bool start_flag;
    bool loop_flag;
    bool constant_volume;
    uint8_t decay;
    uint8_t divider;
    uint8_t period;
    uint8_t duty; // pulse1, pulse2 but not noise
} t_envelope;

typedef struct timer {
    uint16_t divider;
    uint16_t period;
    uint8_t phase;
} t_timer;

typedef struct length_counter {
    bool enabled;
    uint8_t counter;
} t_length_counter;

typedef struct linear_counter {
    bool control_flag;
    uint8_t counter;
    uint8_t period;
} t_linear_counter;

typedef struct channel {
    t_envelope env;
    t_timer timer;
    t_length_counter lc;
    t_linear_counter lin;
} t_channel;

typedef struct apu {
    volatile uint32_t timer_cycles;
    volatile uint32_t cpu_cycles, step_index, prev_step_index;
    volatile uint64_t audio_output_cycles;
    t_channel ch[5];
} t_apu;

typedef struct shell {
    int audio_device;
    int16_t buf[512 * 4];
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
