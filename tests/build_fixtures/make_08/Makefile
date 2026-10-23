ifeq ($(OS),Windows_NT)
EXE = .exe
endif
UNAME := $(shell uname -s)
ARCH = $(shell uname -m)
ifeq ($(UNAME),Darwin)
OS_NAME = darwin
LIBS = -lm
else ifeq ($(UNAME),Linux)
OS_NAME = linux
LIBS = -lm -lpthread
else
OS_NAME = other
endif
ifeq ($(shell uname -m),arm64)
CPU = arm
else
CPU = not-arm
endif
CFLAGS = -DR1='"$(OS_NAME)"' -DR2='"$(CPU)"' -DR3='"$(if $(filter Darwin,$(shell uname -s)),mac,notmac)"' -DR4='"$(ARCH)"' -DR5='"x$(EXE)"'
prog: main.c
	cc $(CFLAGS) main.c $(LIBS) -o prog
