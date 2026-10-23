space := $(subst ,, )
j = $(subst $(space),_,$(strip $(1)))
A = 1
B := $(A)
A = 2
C ?= from-c
D ?= first
D ?= second
E = e
E += f
F := f
F += g
G = $(A)x
H = $(G:x=y)
I := ${A}-${B}
override J = j
NAME = value
ALIAS = $(NAME)
NAME = changed
CFLAGS = -DR1='"$(A)"' -DR2='"$(B)"' -DR3='"$(C)"' -DR4='"$(D)"' -DR5='"$(call j,$(E))"' -DR6='"$(call j,$(F))"' \
	-DR7='"$(H)"' -DR8='"$(I)"' -DR9='"$(J)"' -DR10='"$(ALIAS)"' -DR11='"$$HOME-$(UNDEFINED_VARIABLE)x"'
all: prog
prog: main.c
	cc $(CFLAGS) main.c -o prog
