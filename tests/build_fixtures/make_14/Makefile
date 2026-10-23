WARN = -Wall -Wextra
CFLAGS = $(WARN) -O1 -DR1='"commands"'
OBJ = main.o other.o
prog: $(OBJ)
	cc $(OBJ) -o prog
%.o: %.c
	cc $(CFLAGS) -c $< -o $@
other.o: second.c
	cc $(CFLAGS) -DSPECIAL -c second.c -o other.o
