VPATH = src
.SECONDEXPANSION:
prog: main.c
	cc -DR1='"x"' main.c -o prog
prog:: other
	echo double
%.o: CFLAGS = -O3
$(eval A = 1)
all:
	$(eval B = 2)
out := $(shell echo frozen)
