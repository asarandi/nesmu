CFLAGS += -g -Wall -Werror -Wextra
CFLAGS += -Wno-unused-but-set-parameter -Wno-unused-parameter
CFLAGS += -Wno-unused-but-set-variable -Wno-unused-variable
LDFLAGS += -g
SRC = main.c cpu.c ppu.c

nesmu: $(SRC:.c=.o)
	$(CC) -o $@ $(LDFLAGS) $^

calc: calc.c
	gcc -o calc -Wall -Werror -Wextra -I sha1/ calc.c sha1/sha1.c

format:
	clang-format -i *.c *.h
