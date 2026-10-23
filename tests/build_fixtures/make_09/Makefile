DEBUG ?= 0
ifeq ($(DEBUG),1)
OPT = -O0 -DR1='"debug"'
else
OPT = -O2 -DR1='"release"'
endif
ifdef VERBOSE
OPT += -DR2='"verbose"'
endif
EXTRA ?= none
prog: main.c
	cc $(OPT) -DR3='"$(EXTRA)"' main.c -o prog
