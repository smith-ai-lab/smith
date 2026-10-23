all: prog both.txt a.txt b.txt
prog: main.o
	cc $^ -o $@
%.o: %.c | outdir
	cc -c $< -o $@
%.txt: %.in
	echo "$* $< $@" > $@
both.txt: a.in b.in
	echo "$@|$<|$^|$?" > $@
outdir:
	mkdir -p $@
.PHONY: outdir
