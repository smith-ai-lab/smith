space := $(subst ,, )
comma := ,
j = $(subst $(space),_,$(strip $(1)))
LIST = b a c a
R1 = $(call j,$(sort $(LIST)))
R2 = $(words $(LIST))
R3 = $(call j,$(patsubst %.c,%.o,x.c y.c z.h))
R4 = $(call j,$(filter %.c %.h,x.c y.o z.h))
R5 = $(call j,$(filter-out %.c,x.c y.o z.h))
R6 = $(call j,$(addprefix pre-,a b))
R7 = $(call j,$(addsuffix .x,a b))
R8 = $(call j,$(basename a/b.c d.e))
R9 = $(call j,$(notdir a/b.c d/e.h))
R10 = $(call j,$(dir a/b.c d.h))
R11 = $(call j,$(suffix a/b.c d.h e))
R12 = $(firstword $(LIST))-$(lastword $(LIST))-$(word 2,$(LIST))-$(call j,$(wordlist 2,3,$(LIST)))
R13 = $(call j,$(join a b,1 2))
R14 = $(call j,$(foreach x,1 2 3,$(x)$(x)))
R15 = $(if $(LIST),yes,no)-$(if ,yes,no)-$(or ,,z)-$(and a,b)
R16 = $(findstring b,abc)$(findstring z,abc)x
R17 = $(call j,$(subst a,A,$(LIST)))
R18 = $(call j,$(wildcard *.c))
R19 = $(strip   a   b  )-$(subst $(comma),;,a,b)
R20 = $(call j,$(sort $(foreach x,c a,$(x) b)))
CFLAGS = -DR1='"$(R1)"' -DR2='"$(R2)"' -DR3='"$(R3)"' -DR4='"$(R4)"' -DR5='"$(R5)"' -DR6='"$(R6)"' -DR7='"$(R7)"' \
	-DR8='"$(R8)"' -DR9='"$(R9)"' -DR10='"$(R10)"' -DR11='"$(R11)"' -DR12='"$(R12)"' -DR13='"$(R13)"' \
	-DR14='"$(R14)"' -DR15='"$(R15)"' -DR16='"$(R16)"' -DR17='"$(R17)"' -DR18='"$(R18)"' -DR19='"$(R19)"' \
	-DR20='"$(R20)"'
all: prog
prog: main.c
	cc $(CFLAGS) main.c -o prog
