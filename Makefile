CFLAGS += -Wall -Werror -Wextra -Wno-unused-but-set-parameter -g
LDFLAGS += -g
SRC = main.c cpu.c

nesmu: $(SRC:.c=.o)
	$(CC) -o $@ $(LDFLAGS) $^

calc: calc.c
	gcc -o calc -Wall -Werror -Wextra -I sha1/ calc.c sha1/sha1.c

format:
	clang-format -i *.c
