#include "nesmu.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// https://www.nesdev.org/obelisk-6502-guide/index.html
// https://www.masswerk.at/6502/6502_instruction_set.html
// http://archive.6502.org/datasheets/synertek_hardware_manual.pdf
// ^ appendix a, summary of single cycle execution
// http://www.oxyron.de/html/opcodes02.html

#define CFLAG 0b00000001
#define ZFLAG 0b00000010
#define IFLAG 0b00000100
#define DFLAG 0b00001000
#define BFLAG 0b00010000
#define VFLAG 0b01000000
#define NFLAG 0b10000000

#define IS_CFLAG (cpu->P & CFLAG)
#define IS_ZFLAG (cpu->P & ZFLAG)
#define IS_IFLAG (cpu->P & IFLAG)
#define IS_DFLAG (cpu->P & DFLAG)
#define IS_VFLAG (cpu->P & VFLAG)
#define IS_NFLAG (cpu->P & NFLAG)

#define SET_CFLAG (cpu->P |= CFLAG)
#define SET_ZFLAG (cpu->P |= ZFLAG)
#define SET_IFLAG (cpu->P |= IFLAG)
#define SET_DFLAG (cpu->P |= DFLAG)
#define SET_VFLAG (cpu->P |= VFLAG)
#define SET_NFLAG (cpu->P |= NFLAG)

#define CLEAR_CFLAG (cpu->P &= ~CFLAG)
#define CLEAR_ZFLAG (cpu->P &= ~ZFLAG)
#define CLEAR_IFLAG (cpu->P &= ~IFLAG)
#define CLEAR_DFLAG (cpu->P &= ~DFLAG)
#define CLEAR_VFLAG (cpu->P &= ~VFLAG)
#define CLEAR_NFLAG (cpu->P &= ~NFLAG)

#define UPDATE_ZFLAG(VALUE) ((VALUE) == 0) ? SET_ZFLAG : CLEAR_ZFLAG
#define UPDATE_VFLAG(VALUE) ((VALUE)&VFLAG) ? SET_VFLAG : CLEAR_VFLAG
#define UPDATE_NFLAG(VALUE) ((VALUE)&NFLAG) ? SET_NFLAG : CLEAR_NFLAG

#define UPDATE_ZNFLAGS(VALUE)                                                  \
    UPDATE_ZFLAG(VALUE);                                                       \
    UPDATE_NFLAG(VALUE)

// assuming PC is at beginning of current instruction

static inline uint8_t read_immediate_lo(t_cpu *cpu) {
    return cpu->read(cpu->userdata, cpu->PC + 1);
}

static inline uint8_t read_immediate_hi(t_cpu *cpu) {
    return cpu->read(cpu->userdata, cpu->PC + 2);
}

static inline uint16_t address_absolute(t_cpu *cpu) {
    uint8_t lo = read_immediate_lo(cpu);
    uint8_t hi = read_immediate_hi(cpu);
    return (hi << 8) | lo;
}

static inline uint16_t address_absolute_x(t_cpu *cpu) {
    uint16_t abs = address_absolute(cpu);
    uint16_t abx = abs + cpu->X;
    cpu->extra_cycles = (abs >> 8) != (abx >> 8);
    return abx;
}

static inline uint16_t address_absolute_y(t_cpu *cpu) {
    uint16_t abs = address_absolute(cpu);
    uint16_t aby = abs + cpu->Y;
    cpu->extra_cycles = (abs >> 8) != (aby >> 8);
    return aby;
}

static inline uint16_t indirection(t_cpu *cpu, uint16_t addr_lo,
                                   uint16_t addr_hi) {
    uint8_t lo = cpu->read(cpu->userdata, addr_lo);
    uint8_t hi = cpu->read(cpu->userdata, addr_hi);
    return (hi << 8) | lo;
}

static inline uint16_t address_indirect(t_cpu *cpu) {
    uint16_t addr_lo = address_absolute(cpu);
    uint16_t addr_hi = (addr_lo & 0xff00) | ((addr_lo + 1) & 0x00ff);
    return indirection(cpu, addr_lo, addr_hi);
} // _ind

static inline uint16_t address_indirect_x(t_cpu *cpu) {
    uint8_t addr_lo = read_immediate_lo(cpu) + cpu->X;
    uint8_t addr_hi = addr_lo + 1;
    return indirection(cpu, addr_lo, addr_hi);
} // _idx

static inline uint16_t address_indirect_y(t_cpu *cpu) {
    uint8_t addr_lo = read_immediate_lo(cpu);
    uint8_t addr_hi = addr_lo + 1;
    uint16_t ind = indirection(cpu, addr_lo, addr_hi);
    uint16_t idy = ind + cpu->Y;
    cpu->extra_cycles = (ind >> 8) != (idy >> 8);
    return idy;
} // _idy

static inline uint16_t address_relative(t_cpu *cpu) {
    int16_t addr_rel = (int16_t)(int8_t)read_immediate_lo(cpu);
    uint16_t a = cpu->PC + 2;
    uint16_t b = a + (uint16_t)addr_rel;
    cpu->extra_cycles = ((a >> 8) != (b >> 8)) ? 3 : 1;
    return b;
} // _rel

static uint8_t read(t_cpu *cpu, uint16_t addr) {
    return cpu->read(cpu->userdata, addr);
}

static uint16_t read16(t_cpu *cpu, uint16_t addr) {
    return cpu->read(cpu->userdata, addr) +
           256 * cpu->read(cpu->userdata, addr + 1);
}

static void write(t_cpu *cpu, uint16_t addr, uint8_t val) {
    return cpu->write(cpu->userdata, addr, val);
}

static void stack_push(t_cpu *cpu, uint8_t val) {
    cpu->u16 = cpu->S + 0x100;
    cpu->S--;
    cpu->write(cpu->userdata, cpu->u16, val);
}

static uint8_t stack_pop(t_cpu *cpu) {
    cpu->S++;
    cpu->u16 = cpu->S + 0x100;
    return cpu->read(cpu->userdata, cpu->u16);
}

static void stack_push16(t_cpu *cpu, uint16_t val) {
    stack_push(cpu, val >> 8);
    stack_push(cpu, val & 255);
}

static uint16_t stack_pop16(t_cpu *cpu) {
    return stack_pop(cpu) + 256 * stack_pop(cpu);
}

#define READ_ACCUMULATOR cpu->A
#define WRITE_ACCUMULATOR(VALUE) cpu->A = VALUE
#define READ_REGISTER_X cpu->X
#define WRITE_REGISTER_X(VALUE) cpu->X = VALUE
#define READ_REGISTER_Y cpu->Y
#define WRITE_REGISTER_Y(VALUE) cpu->Y = VALUE

// #define ADDR_IMMEDIATE_LO cpu->PC + 1
#define READ_IMMEDIATE_LO read_immediate_lo(cpu)

#define ADDR_ZERO_PAGE READ_IMMEDIATE_LO
#define READ_ZERO_PAGE read(cpu, ADDR_ZERO_PAGE)

#define READ_ZERO_PAGE_LO read(cpu, ADDR_ZERO_PAGE)
#define READ_ZERO_PAGE_HI read(cpu, (ADDR_ZERO_PAGE + 1) & 255)
#define ADDR_ZERO_PAGE_INDIRECT_WRAPPED                                        \
    ((READ_ZERO_PAGE_LO) + (256 * (READ_ZERO_PAGE_HI)))

#define WRITE_ZERO_PAGE(VALUE) write(cpu, ADDR_ZERO_PAGE, VALUE)
#define ADDR_ZERO_PAGE_X (READ_IMMEDIATE_LO + cpu->X) & 255
#define ADDR_ZERO_PAGE_X_LO ADDR_ZERO_PAGE_X
#define ADDR_ZERO_PAGE_X_HI (READ_IMMEDIATE_LO + cpu->X + 1) & 255

#define READ_ZERO_PAGE_X_LO read(cpu, ADDR_ZERO_PAGE_X_LO)
#define READ_ZERO_PAGE_X READ_ZERO_PAGE_X_LO
#define READ_ZERO_PAGE_X_HI read(cpu, ADDR_ZERO_PAGE_X_HI)

#define WRITE_ZERO_PAGE_X(VALUE) write(cpu, ADDR_ZERO_PAGE_X, VALUE)
#define ADDR_ZERO_PAGE_Y (READ_IMMEDIATE_LO + cpu->Y) & 255
#define READ_ZERO_PAGE_Y read(cpu, ADDR_ZERO_PAGE_Y)
#define WRITE_ZERO_PAGE_Y(VALUE) write(cpu, ADDR_ZERO_PAGE_Y, VALUE)
#define ADDR_RELATIVE cpu->PC + (int16_t)(int8_t)READ_IMMEDIATE_LO
#define READ_RELATIVE read(ADDR_RELATIVE)

#define ADDR_ABSOLUTE address_absolute(cpu)
#define READ_ABSOLUTE read(cpu, ADDR_ABSOLUTE)
#define WRITE_ABSOLUTE(VALUE) write(cpu, ADDR_ABSOLUTE, VALUE)

#define ADDR_ABSOLUTE_X address_absolute_x(cpu)
#define READ_ABSOLUTE_X read(cpu, ADDR_ABSOLUTE_X)
#define WRITE_ABSOLUTE_X(VALUE) write(cpu, ADDR_ABSOLUTE_X, VALUE)

#define ADDR_ABSOLUTE_Y address_absolute_y(cpu)
#define READ_ABSOLUTE_Y read(cpu, ADDR_ABSOLUTE_Y)
#define WRITE_ABSOLUTE_Y(VALUE) write(cpu, ADDR_ABSOLUTE_Y, VALUE)

#define ADDR_INDIRECT address_indirect(cpu)
#define READ_INDIRECT read(cpu, ADDR_INDIRECT)

#define ADDR_INDIRECT_X address_indirect_x(cpu)
#define READ_INDIRECT_X read(cpu, ADDR_INDIRECT_X)
#define WRITE_INDIRECT_X(VALUE) write(cpu, ADDR_INDIRECT_X, VALUE)

#define ADDR_INDIRECT_Y address_indirect_y(cpu)
#define READ_INDIRECT_Y read(cpu, ADDR_INDIRECT_Y)
#define WRITE_INDIRECT_Y(VALUE) write(cpu, ADDR_INDIRECT_Y, VALUE)

#define LOAD_FN(NAME, DST, MACRO)                                              \
    void NAME(t_cpu *cpu) {                                                    \
        DST = MACRO;                                                           \
        UPDATE_ZNFLAGS(DST);                                                   \
    }
LOAD_FN(lda_imm, cpu->A, READ_IMMEDIATE_LO) // 0xa9
LOAD_FN(lda_zpg, cpu->A, READ_ZERO_PAGE)    // 0xa5
LOAD_FN(lda_zpx, cpu->A, READ_ZERO_PAGE_X)  // 0xb5
LOAD_FN(lda_abs, cpu->A, READ_ABSOLUTE)     // 0xad
LOAD_FN(lda_abx, cpu->A, READ_ABSOLUTE_X)   // 0xbd
LOAD_FN(lda_aby, cpu->A, READ_ABSOLUTE_Y)   // 0xb9
LOAD_FN(lda_idx, cpu->A, READ_INDIRECT_X)   // 0xa1
LOAD_FN(lda_idy, cpu->A, READ_INDIRECT_Y)   // 0xb1
LOAD_FN(ldx_imm, cpu->X, READ_IMMEDIATE_LO) // 0xa2
LOAD_FN(ldx_zpg, cpu->X, READ_ZERO_PAGE)    // 0xa6
LOAD_FN(ldx_zpy, cpu->X, READ_ZERO_PAGE_Y)  // 0xb6
LOAD_FN(ldx_abs, cpu->X, READ_ABSOLUTE)     // 0xae
LOAD_FN(ldx_aby, cpu->X, READ_ABSOLUTE_Y)   // 0xbe
LOAD_FN(ldy_imm, cpu->Y, READ_IMMEDIATE_LO) // 0xa0
LOAD_FN(ldy_zpg, cpu->Y, READ_ZERO_PAGE)    // 0xa4
LOAD_FN(ldy_zpx, cpu->Y, READ_ZERO_PAGE_X)  // 0xb4
LOAD_FN(ldy_abs, cpu->Y, READ_ABSOLUTE)     // 0xac
LOAD_FN(ldy_abx, cpu->Y, READ_ABSOLUTE_X)   // 0xbc

#define STORE_FN(NAME, SRC, MACRO)                                             \
    void NAME(t_cpu *cpu) { MACRO(SRC); }
STORE_FN(sta_zpg, cpu->A, WRITE_ZERO_PAGE)   // 0x85
STORE_FN(sta_zpx, cpu->A, WRITE_ZERO_PAGE_X) // 0x95
STORE_FN(sta_abs, cpu->A, WRITE_ABSOLUTE)    // 0x8d
STORE_FN(sta_abx, cpu->A, WRITE_ABSOLUTE_X)  // 0x9d
STORE_FN(sta_aby, cpu->A, WRITE_ABSOLUTE_Y)  // 0x99
STORE_FN(sta_idx, cpu->A, WRITE_INDIRECT_X)  // 0x81
STORE_FN(sta_idy, cpu->A, WRITE_INDIRECT_Y)  // 0x91
STORE_FN(stx_zpg, cpu->X, WRITE_ZERO_PAGE)   // 0x86
STORE_FN(stx_zpy, cpu->X, WRITE_ZERO_PAGE_Y) // 0x96
STORE_FN(stx_abs, cpu->X, WRITE_ABSOLUTE)    // 0x8e
STORE_FN(sty_zpg, cpu->Y, WRITE_ZERO_PAGE)   // 0x84
STORE_FN(sty_zpx, cpu->Y, WRITE_ZERO_PAGE_X) // 0x94
STORE_FN(sty_abs, cpu->Y, WRITE_ABSOLUTE)    // 0x8c

#define AND_FN(NAME, MACRO)                                                    \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->A &= MACRO;                                                       \
        UPDATE_ZNFLAGS(cpu->A);                                                \
    }
AND_FN(and_imm, READ_IMMEDIATE_LO) // 0x29
AND_FN(and_zpg, READ_ZERO_PAGE)    // 0x25
AND_FN(and_zpx, READ_ZERO_PAGE_X)  // 0x35
AND_FN(and_abs, READ_ABSOLUTE)     // 0x2d
AND_FN(and_abx, READ_ABSOLUTE_X)   // 0x3d
AND_FN(and_aby, READ_ABSOLUTE_Y)   // 0x39
AND_FN(and_idx, READ_INDIRECT_X)   // 0x21
AND_FN(and_idy, READ_INDIRECT_Y)   // 0x31

#define EOR_FN(NAME, MACRO)                                                    \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->A ^= MACRO;                                                       \
        UPDATE_ZNFLAGS(cpu->A);                                                \
    }
EOR_FN(eor_imm, READ_IMMEDIATE_LO) // 0x49
EOR_FN(eor_zpg, READ_ZERO_PAGE)    // 0x45
EOR_FN(eor_zpx, READ_ZERO_PAGE_X)  // 0x55
EOR_FN(eor_abs, READ_ABSOLUTE)     // 0x4d
EOR_FN(eor_abx, READ_ABSOLUTE_X)   // 0x5d
EOR_FN(eor_aby, READ_ABSOLUTE_Y)   // 0x59
EOR_FN(eor_idx, READ_INDIRECT_X)   // 0x41
EOR_FN(eor_idy, READ_INDIRECT_Y)   // 0x51

#define ORA_FN(NAME, MACRO)                                                    \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->A |= MACRO;                                                       \
        UPDATE_ZNFLAGS(cpu->A);                                                \
    }
ORA_FN(ora_imm, READ_IMMEDIATE_LO) // 0x09
ORA_FN(ora_zpg, READ_ZERO_PAGE)    // 0x05
ORA_FN(ora_zpx, READ_ZERO_PAGE_X)  // 0x15
ORA_FN(ora_abs, READ_ABSOLUTE)     // 0x0d
ORA_FN(ora_abx, READ_ABSOLUTE_X)   // 0x1d
ORA_FN(ora_aby, READ_ABSOLUTE_Y)   // 0x19
ORA_FN(ora_idx, READ_INDIRECT_X)   // 0x01
ORA_FN(ora_idy, READ_INDIRECT_Y)   // 0x11

#define BIT_FN(NAME, MACRO)                                                    \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = MACRO;                                                       \
        cpu->u8 & cpu->A ? CLEAR_ZFLAG : SET_ZFLAG;                            \
        UPDATE_VFLAG(cpu->u8);                                                 \
        UPDATE_NFLAG(cpu->u8);                                                 \
    }

BIT_FN(bit_zpg, READ_ZERO_PAGE) // 0x24
BIT_FN(bit_abs, READ_ABSOLUTE)  // 0x2c

#define COMPARE_FN(NAME, REGISTER, MACRO)                                      \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = MACRO;                                                       \
        (REGISTER < cpu->u8) ? CLEAR_CFLAG : SET_CFLAG;                        \
        cpu->u8 = REGISTER - cpu->u8;                                          \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
    }

COMPARE_FN(cmp_imm, cpu->A, READ_IMMEDIATE_LO) // 0xc9
COMPARE_FN(cmp_zpg, cpu->A, READ_ZERO_PAGE)    // 0xc5
COMPARE_FN(cmp_zpx, cpu->A, READ_ZERO_PAGE_X)  // 0xd5
COMPARE_FN(cmp_abs, cpu->A, READ_ABSOLUTE)     // 0xcd
COMPARE_FN(cmp_abx, cpu->A, READ_ABSOLUTE_X)   // 0xdd
COMPARE_FN(cmp_aby, cpu->A, READ_ABSOLUTE_Y)   // 0xd9
COMPARE_FN(cmp_idx, cpu->A, READ_INDIRECT_X)   // 0xc1
COMPARE_FN(cmp_idy, cpu->A, READ_INDIRECT_Y)   // 0xd1
COMPARE_FN(cpx_imm, cpu->X, READ_IMMEDIATE_LO) // 0xe0
COMPARE_FN(cpx_zpg, cpu->X, READ_ZERO_PAGE)    // 0xe4
COMPARE_FN(cpx_abs, cpu->X, READ_ABSOLUTE)     // 0xec
COMPARE_FN(cpy_imm, cpu->Y, READ_IMMEDIATE_LO) // 0xc0
COMPARE_FN(cpy_zpg, cpu->Y, READ_ZERO_PAGE)    // 0xc4
COMPARE_FN(cpy_abs, cpu->Y, READ_ABSOLUTE)     // 0xcc

// https://stackoverflow.com/a/29224684
#define IS_ADC_OVERFLOW (~(cpu->A ^ cpu->u8) & (cpu->A ^ cpu->u16) & 0x80)
#define ADDCARRY_FN(NAME, MACRO)                                               \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = MACRO;                                                       \
        cpu->u16 = cpu->A + cpu->u8 + (IS_CFLAG);                              \
        cpu->u16 > 255 ? SET_CFLAG : CLEAR_CFLAG;                              \
        IS_ADC_OVERFLOW ? SET_VFLAG : CLEAR_VFLAG;                             \
        cpu->A = cpu->u16;                                                     \
        UPDATE_ZNFLAGS(cpu->A);                                                \
    }

ADDCARRY_FN(adc_imm, READ_IMMEDIATE_LO) // 0x69
ADDCARRY_FN(adc_zpg, READ_ZERO_PAGE)    // 0x65
ADDCARRY_FN(adc_zpx, READ_ZERO_PAGE_X)  // 0x75
ADDCARRY_FN(adc_abs, READ_ABSOLUTE)     // 0x6d
ADDCARRY_FN(adc_abx, READ_ABSOLUTE_X)   // 0x7d
ADDCARRY_FN(adc_aby, READ_ABSOLUTE_Y)   // 0x79
ADDCARRY_FN(adc_idx, READ_INDIRECT_X)   // 0x61
ADDCARRY_FN(adc_idy, READ_INDIRECT_Y)   // 0x71

#define SUBCARRY_FN(NAME, MACRO)                                               \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = 255 ^ MACRO;                                                 \
        cpu->u16 = cpu->A + cpu->u8 + (IS_CFLAG);                              \
        cpu->u16 > 255 ? SET_CFLAG : CLEAR_CFLAG;                              \
        IS_ADC_OVERFLOW ? SET_VFLAG : CLEAR_VFLAG;                             \
        cpu->A = cpu->u16;                                                     \
        UPDATE_ZNFLAGS(cpu->A);                                                \
    }

SUBCARRY_FN(sbc_imm, READ_IMMEDIATE_LO) // 0xe9
SUBCARRY_FN(sbc_zpg, READ_ZERO_PAGE)    // 0xe5
SUBCARRY_FN(sbc_zpx, READ_ZERO_PAGE_X)  // 0xf5
SUBCARRY_FN(sbc_abs, READ_ABSOLUTE)     // 0xed
SUBCARRY_FN(sbc_abx, READ_ABSOLUTE_X)   // 0xfd
SUBCARRY_FN(sbc_aby, READ_ABSOLUTE_Y)   // 0xf9
SUBCARRY_FN(sbc_idx, READ_INDIRECT_X)   // 0xe1
SUBCARRY_FN(sbc_idy, READ_INDIRECT_Y)   // 0xf1

#define ASL_FN(NAME, READ, WRITE)                                              \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = READ;                                                        \
        cpu->u8 & 128 ? SET_CFLAG : CLEAR_CFLAG;                               \
        cpu->u8 <<= 1;                                                         \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
        WRITE(cpu->u8);                                                        \
    }

ASL_FN(asl_acc, READ_ACCUMULATOR, WRITE_ACCUMULATOR) // 0x0a
ASL_FN(asl_zpg, READ_ZERO_PAGE, WRITE_ZERO_PAGE)     // 0x06
ASL_FN(asl_zpx, READ_ZERO_PAGE_X, WRITE_ZERO_PAGE_X) // 0x16
ASL_FN(asl_abs, READ_ABSOLUTE, WRITE_ABSOLUTE)       // 0x0e
ASL_FN(asl_abx, READ_ABSOLUTE_X, WRITE_ABSOLUTE_X)   // 0x1e

#define LSR_FN(NAME, READ, WRITE)                                              \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = READ;                                                        \
        cpu->u8 & 1 ? SET_CFLAG : CLEAR_CFLAG;                                 \
        cpu->u8 >>= 1;                                                         \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
        WRITE(cpu->u8);                                                        \
    }

LSR_FN(lsr_acc, READ_ACCUMULATOR, WRITE_ACCUMULATOR) // 0x4a
LSR_FN(lsr_zpg, READ_ZERO_PAGE, WRITE_ZERO_PAGE)     // 0x46
LSR_FN(lsr_zpx, READ_ZERO_PAGE_X, WRITE_ZERO_PAGE_X) // 0x56
LSR_FN(lsr_abs, READ_ABSOLUTE, WRITE_ABSOLUTE)       // 0x4e
LSR_FN(lsr_abx, READ_ABSOLUTE_X, WRITE_ABSOLUTE_X)   // 0x5e

#define ROL_FN(NAME, READ, WRITE)                                              \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u16 = READ;                                                       \
        cpu->u16 = (cpu->u16 << 1) | (IS_CFLAG);                               \
        cpu->u16 & 256 ? SET_CFLAG : CLEAR_CFLAG;                              \
        cpu->u8 = cpu->u16;                                                    \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
        WRITE(cpu->u8);                                                        \
    }

ROL_FN(rol_acc, READ_ACCUMULATOR, WRITE_ACCUMULATOR) // 0x2a
ROL_FN(rol_zpg, READ_ZERO_PAGE, WRITE_ZERO_PAGE)     // 0x26
ROL_FN(rol_zpx, READ_ZERO_PAGE_X, WRITE_ZERO_PAGE_X) // 0x36
ROL_FN(rol_abs, READ_ABSOLUTE, WRITE_ABSOLUTE)       // 0x2e
ROL_FN(rol_abx, READ_ABSOLUTE_X, WRITE_ABSOLUTE_X)   // 0x3e

#define ROR_FN(NAME, READ, WRITE)                                              \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u16 = READ;                                                       \
        cpu->u16 |= (IS_CFLAG) << 8;                                           \
        cpu->u16 & 1 ? SET_CFLAG : CLEAR_CFLAG;                                \
        cpu->u16 >>= 1;                                                        \
        cpu->u8 = cpu->u16;                                                    \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
        WRITE(cpu->u8);                                                        \
    }

ROR_FN(ror_acc, READ_ACCUMULATOR, WRITE_ACCUMULATOR) // 0x6a
ROR_FN(ror_zpg, READ_ZERO_PAGE, WRITE_ZERO_PAGE)     // 0x66
ROR_FN(ror_zpx, READ_ZERO_PAGE_X, WRITE_ZERO_PAGE_X) // 0x76
ROR_FN(ror_abs, READ_ABSOLUTE, WRITE_ABSOLUTE)       // 0x6e
ROR_FN(ror_abx, READ_ABSOLUTE_X, WRITE_ABSOLUTE_X)   // 0x7e

#define DECREMENT_FN(NAME, READ, WRITE)                                        \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = READ;                                                        \
        cpu->u8 -= 1;                                                          \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
        WRITE(cpu->u8);                                                        \
    }

DECREMENT_FN(dec_zpg, READ_ZERO_PAGE, WRITE_ZERO_PAGE)     // 0xc6
DECREMENT_FN(dec_zpx, READ_ZERO_PAGE_X, WRITE_ZERO_PAGE_X) // 0xd6
DECREMENT_FN(dec_abs, READ_ABSOLUTE, WRITE_ABSOLUTE)       // 0xce
DECREMENT_FN(dec_abx, READ_ABSOLUTE_X, WRITE_ABSOLUTE_X)   // 0xde
DECREMENT_FN(dex_imp, READ_REGISTER_X, WRITE_REGISTER_X)   // 0xca
DECREMENT_FN(dey_imp, READ_REGISTER_Y, WRITE_REGISTER_Y)   // 0x88

#define INCREMENT_FN(NAME, READ, WRITE)                                        \
    void NAME(t_cpu *cpu) {                                                    \
        cpu->u8 = READ;                                                        \
        cpu->u8 += 1;                                                          \
        UPDATE_ZNFLAGS(cpu->u8);                                               \
        WRITE(cpu->u8);                                                        \
    }

INCREMENT_FN(inc_zpg, READ_ZERO_PAGE, WRITE_ZERO_PAGE)     // 0xe6
INCREMENT_FN(inc_zpx, READ_ZERO_PAGE_X, WRITE_ZERO_PAGE_X) // 0xf6
INCREMENT_FN(inc_abs, READ_ABSOLUTE, WRITE_ABSOLUTE)       // 0xee
INCREMENT_FN(inc_abx, READ_ABSOLUTE_X, WRITE_ABSOLUTE_X)   // 0xfe
INCREMENT_FN(inx_imp, READ_REGISTER_X, WRITE_REGISTER_X)   // 0xe8
INCREMENT_FN(iny_imp, READ_REGISTER_Y, WRITE_REGISTER_Y)   // 0xc8

void bcc_rel(t_cpu *cpu) {
    cpu->PC = (IS_CFLAG) ? cpu->PC : address_relative(cpu);
} // 0x90
void bcs_rel(t_cpu *cpu) {
    cpu->PC = (IS_CFLAG) ? address_relative(cpu) : cpu->PC;
} // 0xb0
void bne_rel(t_cpu *cpu) {
    cpu->PC = (IS_ZFLAG) ? cpu->PC : address_relative(cpu);
} // 0xd0
void beq_rel(t_cpu *cpu) {
    cpu->PC = (IS_ZFLAG) ? address_relative(cpu) : cpu->PC;
} // 0xf0
void bpl_rel(t_cpu *cpu) {
    cpu->PC = (IS_NFLAG) ? cpu->PC : address_relative(cpu);
} // 0x10
void bmi_rel(t_cpu *cpu) {
    cpu->PC = (IS_NFLAG) ? address_relative(cpu) : cpu->PC;
} // 0x30
void bvc_rel(t_cpu *cpu) {
    cpu->PC = (IS_VFLAG) ? cpu->PC : address_relative(cpu);
} // 0x50
void bvs_rel(t_cpu *cpu) {
    cpu->PC = (IS_VFLAG) ? address_relative(cpu) : cpu->PC;
} // 0x70

#define TRANSFER_FN(NAME, SRC, DST)                                            \
    void NAME(t_cpu *cpu) {                                                    \
        DST = SRC;                                                             \
        UPDATE_ZNFLAGS(DST);                                                   \
    }
TRANSFER_FN(tax_imp, cpu->A, cpu->X)          // 0xaa
TRANSFER_FN(tay_imp, cpu->A, cpu->Y)          // 0xa8
TRANSFER_FN(tsx_imp, cpu->S, cpu->X)          // 0xba
TRANSFER_FN(txa_imp, cpu->X, cpu->A)          // 0x8a
void txs_imp(t_cpu *cpu) { cpu->S = cpu->X; } // 0x9a
TRANSFER_FN(tya_imp, cpu->Y, cpu->A)          // 0x98

void nop_imp(t_cpu *cpu) { (void)cpu; } // 0xea

void pha_imp(t_cpu *cpu) { stack_push(cpu, cpu->A); }              // 0x48
void php_imp(t_cpu *cpu) { stack_push(cpu, cpu->P | 0b00110000); } // 0x08
void pla_imp(t_cpu *cpu) {
    cpu->A = stack_pop(cpu);
    UPDATE_ZNFLAGS(cpu->A);
} // 0x68
void plp_imp(t_cpu *cpu) { cpu->P = stack_pop(cpu); } // 0x28

void clc_imp(t_cpu *cpu) { CLEAR_CFLAG; } // 0x18
void cld_imp(t_cpu *cpu) { CLEAR_DFLAG; } // 0xd8
void cli_imp(t_cpu *cpu) { CLEAR_IFLAG; } // 0x58
void clv_imp(t_cpu *cpu) { CLEAR_VFLAG; } // 0xb8

void sec_imp(t_cpu *cpu) { SET_CFLAG; } // 0x38
void sed_imp(t_cpu *cpu) { SET_DFLAG; } // 0xf8
void sei_imp(t_cpu *cpu) { SET_IFLAG; } // 0x78

void brk_imp(t_cpu *cpu) {
    stack_push16(cpu, cpu->PC + 2);
    stack_push(cpu, cpu->P | 0b00110000);
    cpu->P |= IFLAG;
    cpu->PC = read16(cpu, 0xfffe);
} // 0x00

void rti_imp(t_cpu *cpu) {
    cpu->P = stack_pop(cpu) & ~BFLAG;
    cpu->PC = stack_pop16(cpu);
} // 0x40
void rts_imp(t_cpu *cpu) {
    cpu->PC = stack_pop16(cpu);
    cpu->PC++;
} // 0x60

void jmp_abs(t_cpu *cpu) { cpu->PC = ADDR_ABSOLUTE; } // 0x4c
void jmp_ind(t_cpu *cpu) { cpu->PC = ADDR_INDIRECT; } // 0x6c
void jsr_abs(t_cpu *cpu) {
    // u16 is clobbered
    uint16_t newPC = ADDR_ABSOLUTE;
    stack_push16(cpu, cpu->PC + 2);
    cpu->PC = newPC;
} // 0x20

enum mode {
    absolute,
    absolute_x,
    absolute_y,
    accumulator,
    immediate,
    implied,
    indirect,
    indirect_x,
    indirect_y,
    relative,
    zero_page,
    zero_page_x,
    zero_page_y,
};

typedef struct instruction {
    uint8_t opcode;
    void (*fn)(t_cpu *);
    char *name;
    uint8_t mode;
    uint8_t num_bytes;
    uint8_t num_cycles;
    bool has_extra_cycles;
} t_ins;

struct instruction insns[] = {
    {0x69, adc_imm, "adc", immediate, 2, 2, false},
    {0x65, adc_zpg, "adc", zero_page, 2, 3, false},
    {0x75, adc_zpx, "adc", zero_page_x, 2, 4, false},
    {0x6d, adc_abs, "adc", absolute, 3, 4, false},
    {0x7d, adc_abx, "adc", absolute_x, 3, 4, true},
    {0x79, adc_aby, "adc", absolute_y, 3, 4, true},
    {0x61, adc_idx, "adc", indirect_x, 2, 6, false},
    {0x71, adc_idy, "adc", indirect_y, 2, 5, true},
    {0x29, and_imm, "and", immediate, 2, 2, false},
    {0x25, and_zpg, "and", zero_page, 2, 3, false},
    {0x35, and_zpx, "and", zero_page_x, 2, 4, false},
    {0x2d, and_abs, "and", absolute, 3, 4, false},
    {0x3d, and_abx, "and", absolute_x, 3, 4, true},
    {0x39, and_aby, "and", absolute_y, 3, 4, true},
    {0x21, and_idx, "and", indirect_x, 2, 6, false},
    {0x31, and_idy, "and", indirect_y, 2, 5, true},
    {0x0a, asl_acc, "asl", accumulator, 1, 2, false},
    {0x06, asl_zpg, "asl", zero_page, 2, 5, false},
    {0x16, asl_zpx, "asl", zero_page_x, 2, 6, false},
    {0x0e, asl_abs, "asl", absolute, 3, 6, false},
    {0x1e, asl_abx, "asl", absolute_x, 3, 7, false},
    {0x90, bcc_rel, "bcc", relative, 2, 2, true},
    {0xb0, bcs_rel, "bcs", relative, 2, 2, true},
    {0xf0, beq_rel, "beq", relative, 2, 2, true},
    {0x30, bmi_rel, "bmi", relative, 2, 2, true},
    {0xd0, bne_rel, "bne", relative, 2, 2, true},
    {0x10, bpl_rel, "bpl", relative, 2, 2, true},
    {0x50, bvc_rel, "bvc", relative, 2, 2, true},
    {0x70, bvs_rel, "bvs", relative, 2, 2, true},
    {0x24, bit_zpg, "bit", zero_page, 2, 3, false},
    {0x2c, bit_abs, "bit", absolute, 3, 4, false},
    {0x00, brk_imp, "brk", implied, 1, 7, false},
    {0x18, clc_imp, "clc", implied, 1, 2, false},
    {0xd8, cld_imp, "cld", implied, 1, 2, false},
    {0x58, cli_imp, "cli", implied, 1, 2, false},
    {0xb8, clv_imp, "clv", implied, 1, 2, false},
    {0xc9, cmp_imm, "cmp", immediate, 2, 2, false},
    {0xc5, cmp_zpg, "cmp", zero_page, 2, 3, false},
    {0xd5, cmp_zpx, "cmp", zero_page_x, 2, 4, false},
    {0xcd, cmp_abs, "cmp", absolute, 3, 4, false},
    {0xdd, cmp_abx, "cmp", absolute_x, 3, 4, true},
    {0xd9, cmp_aby, "cmp", absolute_y, 3, 4, true},
    {0xc1, cmp_idx, "cmp", indirect_x, 2, 6, false},
    {0xd1, cmp_idy, "cmp", indirect_y, 2, 5, true},
    {0xe0, cpx_imm, "cpx", immediate, 2, 2, false},
    {0xe4, cpx_zpg, "cpx", zero_page, 2, 3, false},
    {0xec, cpx_abs, "cpx", absolute, 3, 4, false},
    {0xc0, cpy_imm, "cpy", immediate, 2, 2, false},
    {0xc4, cpy_zpg, "cpy", zero_page, 2, 3, false},
    {0xcc, cpy_abs, "cpy", absolute, 3, 4, false},
    {0xc6, dec_zpg, "dec", zero_page, 2, 5, false},
    {0xd6, dec_zpx, "dec", zero_page_x, 2, 6, false},
    {0xce, dec_abs, "dec", absolute, 3, 6, false},
    {0xde, dec_abx, "dec", absolute_x, 3, 7, false},
    {0xca, dex_imp, "dex", implied, 1, 2, false},
    {0x88, dey_imp, "dey", implied, 1, 2, false},
    {0x49, eor_imm, "eor", immediate, 2, 2, false},
    {0x45, eor_zpg, "eor", zero_page, 2, 3, false},
    {0x55, eor_zpx, "eor", zero_page_x, 2, 4, false},
    {0x4d, eor_abs, "eor", absolute, 3, 4, false},
    {0x5d, eor_abx, "eor", absolute_x, 3, 4, true},
    {0x59, eor_aby, "eor", absolute_y, 3, 4, true},
    {0x41, eor_idx, "eor", indirect_x, 2, 6, false},
    {0x51, eor_idy, "eor", indirect_y, 2, 5, true},
    {0xe6, inc_zpg, "inc", zero_page, 2, 5, false},
    {0xf6, inc_zpx, "inc", zero_page_x, 2, 6, false},
    {0xee, inc_abs, "inc", absolute, 3, 6, false},
    {0xfe, inc_abx, "inc", absolute_x, 3, 7, false},
    {0xe8, inx_imp, "inx", implied, 1, 2, false},
    {0xc8, iny_imp, "iny", implied, 1, 2, false},
    {0x4c, jmp_abs, "jmp", absolute, 3, 3, false},
    {0x6c, jmp_ind, "jmp", indirect, 3, 5, false},
    {0x20, jsr_abs, "jsr", absolute, 3, 6, false},
    {0xa9, lda_imm, "lda", immediate, 2, 2, false},
    {0xa5, lda_zpg, "lda", zero_page, 2, 3, false},
    {0xb5, lda_zpx, "lda", zero_page_x, 2, 4, false},
    {0xad, lda_abs, "lda", absolute, 3, 4, false},
    {0xbd, lda_abx, "lda", absolute_x, 3, 4, true},
    {0xb9, lda_aby, "lda", absolute_y, 3, 4, true},
    {0xa1, lda_idx, "lda", indirect_x, 2, 6, false},
    {0xb1, lda_idy, "lda", indirect_y, 2, 5, true},
    {0xa2, ldx_imm, "ldx", immediate, 2, 2, false},
    {0xa6, ldx_zpg, "ldx", zero_page, 2, 3, false},
    {0xb6, ldx_zpy, "ldx", zero_page_y, 2, 4, false},
    {0xae, ldx_abs, "ldx", absolute, 3, 4, false},
    {0xbe, ldx_aby, "ldx", absolute_y, 3, 4, true},
    {0xa0, ldy_imm, "ldy", immediate, 2, 2, false},
    {0xa4, ldy_zpg, "ldy", zero_page, 2, 3, false},
    {0xb4, ldy_zpx, "ldy", zero_page_x, 2, 4, false},
    {0xac, ldy_abs, "ldy", absolute, 3, 4, false},
    {0xbc, ldy_abx, "ldy", absolute_x, 3, 4, true},
    {0x4a, lsr_acc, "lsr", accumulator, 1, 2, false},
    {0x46, lsr_zpg, "lsr", zero_page, 2, 5, false},
    {0x56, lsr_zpx, "lsr", zero_page_x, 2, 6, false},
    {0x4e, lsr_abs, "lsr", absolute, 3, 6, false},
    {0x5e, lsr_abx, "lsr", absolute_x, 3, 7, false},
    {0xea, nop_imp, "nop", implied, 1, 2, false},
    {0x09, ora_imm, "ora", immediate, 2, 2, false},
    {0x05, ora_zpg, "ora", zero_page, 2, 3, false},
    {0x15, ora_zpx, "ora", zero_page_x, 2, 4, false},
    {0x0d, ora_abs, "ora", absolute, 3, 4, false},
    {0x1d, ora_abx, "ora", absolute_x, 3, 4, true},
    {0x19, ora_aby, "ora", absolute_y, 3, 4, true},
    {0x01, ora_idx, "ora", indirect_x, 2, 6, false},
    {0x11, ora_idy, "ora", indirect_y, 2, 5, true},
    {0x48, pha_imp, "pha", implied, 1, 3, false},
    {0x08, php_imp, "php", implied, 1, 3, false},
    {0x68, pla_imp, "pla", implied, 1, 4, false},
    {0x28, plp_imp, "plp", implied, 1, 4, false},
    {0x2a, rol_acc, "rol", accumulator, 1, 2, false},
    {0x26, rol_zpg, "rol", zero_page, 2, 5, false},
    {0x36, rol_zpx, "rol", zero_page_x, 2, 6, false},
    {0x2e, rol_abs, "rol", absolute, 3, 6, false},
    {0x3e, rol_abx, "rol", absolute_x, 3, 7, false},
    {0x6a, ror_acc, "ror", accumulator, 1, 2, false},
    {0x66, ror_zpg, "ror", zero_page, 2, 5, false},
    {0x76, ror_zpx, "ror", zero_page_x, 2, 6, false},
    {0x6e, ror_abs, "ror", absolute, 3, 6, false},
    {0x7e, ror_abx, "ror", absolute_x, 3, 7, false},
    {0x40, rti_imp, "rti", implied, 1, 6, false},
    {0x60, rts_imp, "rts", implied, 1, 6, false},
    {0xe9, sbc_imm, "sbc", immediate, 2, 2, false},
    {0xe5, sbc_zpg, "sbc", zero_page, 2, 3, false},
    {0xf5, sbc_zpx, "sbc", zero_page_x, 2, 4, false},
    {0xed, sbc_abs, "sbc", absolute, 3, 4, false},
    {0xfd, sbc_abx, "sbc", absolute_x, 3, 4, true},
    {0xf9, sbc_aby, "sbc", absolute_y, 3, 4, true},
    {0xe1, sbc_idx, "sbc", indirect_x, 2, 6, false},
    {0xf1, sbc_idy, "sbc", indirect_y, 2, 5, true},
    {0x38, sec_imp, "sec", implied, 1, 2, false},
    {0xf8, sed_imp, "sed", implied, 1, 2, false},
    {0x78, sei_imp, "sei", implied, 1, 2, false},
    {0x85, sta_zpg, "sta", zero_page, 2, 3, false},
    {0x95, sta_zpx, "sta", zero_page_x, 2, 4, false},
    {0x8d, sta_abs, "sta", absolute, 3, 4, false},
    {0x9d, sta_abx, "sta", absolute_x, 3, 5, false},
    {0x99, sta_aby, "sta", absolute_y, 3, 5, false},
    {0x81, sta_idx, "sta", indirect_x, 2, 6, false},
    {0x91, sta_idy, "sta", indirect_y, 2, 6, false},
    {0x86, stx_zpg, "stx", zero_page, 2, 3, false},
    {0x96, stx_zpy, "stx", zero_page_y, 2, 4, false},
    {0x8e, stx_abs, "stx", absolute, 3, 4, false},
    {0x84, sty_zpg, "sty", zero_page, 2, 3, false},
    {0x94, sty_zpx, "sty", zero_page_x, 2, 4, false},
    {0x8c, sty_abs, "sty", absolute, 3, 4, false},
    {0xaa, tax_imp, "tax", implied, 1, 2, false},
    {0xa8, tay_imp, "tay", implied, 1, 2, false},
    {0xba, tsx_imp, "tsx", implied, 1, 2, false},
    {0x8a, txa_imp, "txa", implied, 1, 2, false},
    {0x9a, txs_imp, "txs", implied, 1, 2, false},
    {0x98, tya_imp, "tya", implied, 1, 2, false},
};

//
// +-------------+
// |table 1 notes|
// +-------------+
// abbr.   what it means
// -----   -------------
// IMD     #$xx
// REL     $xx,PC
// 0PG     $xx
// 0PX     $xx,X
// 0PY     $xx,Y
// ABS     $xxxx
// ABX     $xxxx,X
// ABY     $xxxx,Y
// IND     ($xxxx)
// NDX     ($xx,X)
// NDY     ($xx),Y
//

// 1 byte  accumulator
// 1 byte  implied
//
// 2 bytes immediate
// 2 bytes indirect_x
// 2 bytes indirect_y
// 2 bytes relative
// 2 bytes zero_page
// 2 bytes zero_page_x
// 2 bytes zero_page_y
//
// 3 bytes absolute
// 3 bytes absolute_x
// 3 bytes absolute_y
// 3 bytes indirect

t_ins *get_instruction(uint8_t opcode) {
    for (int i = 0; i < 151; i++) {
        if (insns[i].opcode == opcode)
            return &insns[i];
    }
    return NULL;
}

static bool is_endless_loop(t_cpu *cpu) {
    uint8_t a = read(cpu, cpu->PC);
    uint8_t b = read(cpu, cpu->PC + 1);
    return a == 0xf0 && b == 0xfe && (IS_ZFLAG);
}

int run_opcode(t_cpu *cpu, bool debug) {
    uint16_t opcode, pc;
    int retval;

    opcode = cpu->read(cpu->userdata, cpu->PC);
    t_ins *ins = get_instruction(opcode);

    if (debug) {
        printf(
            "PC: %04x Ins: %s Bytes: %02x%02x%02x A:%02X X:%02X Y:%02X P:%02X "
            "SP:%02X PPU:%3d,%3d VBL:%d CYC:%d\n",
            cpu->PC, ins ? ins->name : "???", read(cpu, cpu->PC),
            read(cpu, cpu->PC + 1), read(cpu, cpu->PC + 2), cpu->A, cpu->X,
            cpu->Y, cpu->P | 0b00100000, cpu->S, ppu_get_y(), ppu_get_x(),
            ppu_read(0, 0x2002) == 0 ? 0 : 1, cpu->cycles);
    }

    if (!ins) {
        printf("unknown instruction %02x\n", opcode);
        exit(1);
    }

    if (is_endless_loop(cpu)) {
        printf("endless loop detected\n");
        exit(1);
    }

    pc = cpu->PC;
    cpu->extra_cycles = 0;
    ins->fn(cpu);
    if (cpu->PC == pc) {
        cpu->PC += ins->num_bytes;
    }

    retval = ins->num_cycles;
    retval += ins->has_extra_cycles ? cpu->extra_cycles : 0;
    return retval;
}
