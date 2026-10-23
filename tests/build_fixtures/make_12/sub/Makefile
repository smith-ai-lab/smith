CFLAGS = -O1
all: lib.a
lib.a: lib.o
	ar rcs $@ $^
lib.o: lib.c
	cc $(CFLAGS) -c lib.c -o lib.o
