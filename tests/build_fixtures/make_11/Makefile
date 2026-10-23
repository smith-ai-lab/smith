all: prog
libx.a: lib.o
	ar rcs $@ $^
lib.o: lib.c
	cc -c lib.c -o lib.o
prog: main.o libx.a
	cc main.o libx.a -o prog
main.o: main.c
	cc -DR1='"archive"' -c main.c -o main.o
