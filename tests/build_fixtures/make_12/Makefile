all:
	$(MAKE) prog
prog: main.c sub/lib.a
	cc -DR1='"recursive"' main.c sub/lib.a -o prog
sub/lib.a:
	$(MAKE) -C sub
