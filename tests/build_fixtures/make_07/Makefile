MODE = fast
EMPTY =
ifeq ($(MODE),fast)
R1 = fast
else
R1 = slow
endif
ifneq ($(MODE),fast)
R2 = not-fast
else ifeq ($(MODE),slow)
R2 = slow
else
R2 = other
endif
ifdef MODE
R3 = defined
endif
ifdef EMPTY
R4 = defined
else
R4 = empty
endif
ifndef NEVER
R5 = undefined
endif
ifeq "a" 'a'
R6 = quoted
endif
ifneq ($(MODE),)
  ifeq ($(MODE),fast)
R7 = nested
  endif
endif
CFLAGS = -DR1='"$(R1)"' -DR2='"$(R2)"' -DR3='"$(R3)"' -DR4='"$(R4)"' -DR5='"$(R5)"' -DR6='"$(R6)"' -DR7='"$(R7)"'
prog: main.c
	cc $(CFLAGS) main.c -o prog
