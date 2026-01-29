CFLAGS += -Wno-unused-but-set-parameter
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wno-unused-but-set-variable
CFLAGS += -Wno-unused-variable
CFLAGS += -Wno-unused-function

CFLAGS += -g -Wall -Werror -Wextra
CFLAGS += $(shell sdl2-config --cflags)
LDFLAGS += -g $(shell sdl2-config --libs) -lm
SRC = main.c cpu.c ppu.c apu.c shell.c

nesmu: $(SRC:.c=.o)
	$(CC) -o $@ $^ $(LDFLAGS)

fclean:
	rm -f nesmu *.o

format:
	clang-format -i *.c *.h
