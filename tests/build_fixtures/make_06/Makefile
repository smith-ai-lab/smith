include vars.mk
-include missing.mk
OBJ = main.o
-include $(wildcard build/*.d)
DEPS = $(OBJ:.o=.d)
-include $(DEPS)
CFLAGS = -DR1='"$(FROM_INCLUDE)"'
prog: $(OBJ)
	cc $^ -o $@
%.o: %.c
	cc $(CFLAGS) -MMD -MP -c $< -o $@
