CFLAGS += -g -Wall -Werror -Wextra
CFLAGS += -Wno-unused-but-set-parameter -Wno-unused-parameter
CFLAGS += -Wno-unused-but-set-variable -Wno-unused-variable
CFLAGS += $(shell sdl2-config --cflags)
LDFLAGS += -g $(shell sdl2-config --libs)
SRC = main.c cpu.c ppu.c apu.c shell.c

nesmu: $(SRC:.c=.o)
	$(CC) -o $@ $(LDFLAGS) $^

calc: calc.c
	gcc -o calc -Wall -Werror -Wextra -I sha1/ calc.c sha1/sha1.c

format:
	clang-format -i *.c *.h
