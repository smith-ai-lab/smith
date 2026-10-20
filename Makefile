# Builds smith with any C compiler: it is one file and needs nothing but libc. STD=gnu23 works on a recent clang.
# The build directory is named for the OS and architecture, so a binary built on one machine is never mistaken for
# an up-to-date one on another (this folder is shared with a Linux VM as well as macOS).
CC ?= cc
STD ?= gnu17
WERROR ?= -Werror
CFLAGS ?= -std=$(STD) -O1 -g -Wall -Wextra -Wpedantic $(WERROR)
BUILD ?= build/$(shell uname -s)-$(shell uname -m)

.PHONY: all test compare clean

all: $(BUILD)/smith $(BUILD)/smith++

$(BUILD)/smith: smith.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) smith.c -o $@

# The same program under another name, as clang++ is to clang: it compiles and links C++ with clang++.
$(BUILD)/smith++: $(BUILD)/smith
	ln -sf smith $@

# Routing tests with stub compilers: no clang or Xcode needed, and no real build is touched.
test: $(BUILD)/smith
	CC='$(CC)' sh tests/run.sh $(BUILD)/smith

# Runs a table of invocations through a real compiler and through smith, and compares stdout, stderr, exit status and
# the files produced. REAL is the compiler, for example REAL=$$(command -v clang).
compare: $(BUILD)/smith
	@test -n '$(REAL)' || { echo 'usage: make compare REAL=/path/to/clang'; exit 2; }
	sh tests/compare.sh $(BUILD)/smith '$(REAL)'

clean:
	rm -rf build
