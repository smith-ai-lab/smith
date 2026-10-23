.PHONY: all clean test run check lint extra
first: prog
all: prog extra
prog: main.c
	cc main.c -o prog
test: prog
	./prog
run: prog
	./prog
check: lint test
lint:
	true
clean:
	rm -f prog
extra:
	echo extra > extra.txt
