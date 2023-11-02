#include "nesmu.h"
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

uint8_t cpu_read(void *userdata, uint16_t addr) {
    uint8_t *memory = (uint8_t *)userdata;

    if (addr < 0x2000) {
        // RAM
        return memory[addr & 0x7ff];
    } else if (0x2000 <= addr && addr < 0x4000) {
        // PPU
        //    printf("PPU read, addr: %04x, value: %02x\n",
        //    0x2000 + (addr & 7), memory[0x2000 + (addr & 7)]);
        return ppu_read(userdata, 0x2000 + (addr & 7));
    } else if (0x4000 <= addr && addr < 0x4020) {
        // APU
        printf("APU read, addr: %04x, value: %02x\n", addr, memory[addr]);
        return memory[addr];
    }

    return memory[addr];
}

void cpu_write(void *userdata, uint16_t addr, uint8_t val) {
    uint8_t *memory = (uint8_t *)userdata;

    if (0x6004 <= addr && addr < 0x6100) {
        if (val)
            putchar(val);
    }

    if (addr < 0x2000) {
        // RAM
        memory[addr & 0x7ff] = val;
        return;
    } else if (0x2000 <= addr && addr < 0x4000) {
        // PPU
        //        printf("PPU write, addr: %04x, value: %02x\n", 0x2000 + (addr
        //        & 7), val);
        ppu_write(userdata, 0x2000 + (addr & 7), val);
        return;
    } else if (0x4000 <= addr && addr < 0x4020) {
        printf("APU write, addr: %04x, value: %02x\n", addr, val);
        // APU
        memory[addr] = val;
        return;
    }

    memory[addr] = val;
    return;
}

int main(int argc, char *argv[]) {
    int fd, ret, opt, i, debug = 0;
    struct stat st;
    size_t size;
    void *data;

    t_nes mynes;
    t_nes *nes = &mynes;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            debug = 1;
            break;
        default: /* '?' */
            fprintf(stderr, "usage: %s [-d] rom\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "expected argument after options\n");
        exit(EXIT_FAILURE);
    }

    fd = open(argv[optind], O_RDONLY);
    if (fd == -1) {
        perror("open()");
        return 1;
    }

    ret = fstat(fd, &st);
    if (ret != 0) {
        perror("fstat()");
        close(fd);
        return 1;
    }
    size = st.st_size;

    data = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == NULL) {
        perror("mmap()");
        close(fd);
        return 1;
    }
    (void)close(fd);

    memset(nes, 0, sizeof(*nes));

    if (size == 16 + 32768 + 8192) {
        memcpy(nes->memory + 0x8000, data + 16, 0x8000);
    } else if (size == 16 + 16384 + 8192) {
        memcpy(nes->memory + 0x8000, data + 16, 0x4000);
        memcpy(nes->memory + 0xc000, data + 16, 0x4000);
    } else {
        assert(false);
    }
    (void)munmap(data, size);

    nes->cpu.S = 0xfd;
    nes->cpu.P = 0x24;
    nes->cpu.userdata = nes;
    nes->cpu.read = cpu_read;
    nes->cpu.write = cpu_write;
    nes->cpu.PC = nes->memory[0xfffc] + 256 * nes->memory[0xfffd];
    // nes->cpu.PC = 0xc000;
    nes->cpu.cycles = 7; // nestest.log, nintendulator

    for (;;) {
        ppu_update(nes);
        nes->cpu.cycles += run_opcode(nes, debug);
    }

    return 0;
}
