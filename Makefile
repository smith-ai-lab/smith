# Builds smith with any C compiler: it is one file and needs nothing but libc. STD=gnu23 works on a recent clang.
# The build directory is named for the OS and architecture, so a binary built on one machine is never mistaken for
# an up-to-date one on another (this folder is shared with a Linux VM as well as macOS).
CC ?= cc
STD ?= gnu17
WERROR ?= -Werror
CFLAGS ?= -std=$(STD) -O1 -g -Wall -Wextra -Wpedantic $(WERROR)
BUILD ?= build/$(shell uname -s)-$(shell uname -m)
PREFIX ?= $(HOME)/.local

.PHONY: all test compare install clean

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

# Copies smith, once its tests pass, to $(PREFIX)/bin, where projects find it as CC. A link to the build directory would
# hand every project each half-finished build. The rename replaces the binary in one step, so a build running meanwhile
# never sees half a file.
install: test
	mkdir -p $(PREFIX)/bin
	cp $(BUILD)/smith $(PREFIX)/bin/smith.tmp
	mv $(PREFIX)/bin/smith.tmp $(PREFIX)/bin/smith
	ln -sf smith $(PREFIX)/bin/smith++

clean:
	rm -rf build
