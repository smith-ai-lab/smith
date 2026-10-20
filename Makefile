# Builds smith with any C compiler: it is one file and needs nothing but libc. STD=gnu23 works on a recent clang.
# The build directory is named for the OS and architecture, so a binary built on one machine is never mistaken for
# an up-to-date one on another (this folder is shared with a Linux VM as well as macOS).
CC ?= cc
STD ?= gnu17
WERROR ?= -Werror
CFLAGS ?= -std=$(STD) -O1 -g -Wall -Wextra -Wpedantic $(WERROR)
BUILD ?= build/$(shell uname -s)-$(shell uname -m)
PREFIX ?= $(HOME)/.local
# The compiler `make compare` checks smith against; Apple's clang is the one smith picks on macOS.
REAL ?= $(if $(filter Darwin,$(shell uname -s)),/usr/bin/clang)

# Expanded only by the targets that use them, so plain make never runs brew.
LLVM_MAJOR := 23
LLVM_BIN ?= $(shell brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)/bin
CLANG_FORMAT = $(LLVM_BIN)/clang-format
CLANG_TIDY = $(LLVM_BIN)/clang-tidy
FORMAT_SOURCES := smith.c tests/stub.c

.PHONY: all test compare install clean format check check-llvm check-format check-tidy

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
# the files produced. REAL is the compiler: /usr/bin/clang on macOS, else for example REAL=$$(command -v clang).
compare: $(BUILD)/smith
	@test -n '$(REAL)' || { echo 'usage: make compare REAL=/path/to/clang'; exit 2; }
	sh tests/compare.sh $(BUILD)/smith '$(REAL)'

# Rewrites the sources in the style of .clang-format.
format: check-llvm
	$(CLANG_FORMAT) -i $(FORMAT_SOURCES)

# Formatting, lint, the routing tests and the comparison with the real compiler.
check: check-format check-tidy test compare

# The LLVM tools must be one major version, so every machine formats and lints alike.
check-llvm:
	@for tool in $(CLANG_FORMAT) $(CLANG_TIDY); do \
		major=$$($$tool --version 2>/dev/null | sed -n 's/.*version \([0-9][0-9]*\)\..*/\1/p' | head -n 1); \
		if [ "$$major" != $(LLVM_MAJOR) ]; then \
			echo "$$tool: need LLVM $(LLVM_MAJOR), found $${major:-none}; install it with brew install llvm or set LLVM_BIN"; \
			exit 1; \
		fi; \
	done

# Fails on any file clang-format would change; make format fixes them.
check-format: check-llvm
	@$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_SOURCES)

# clang-tidy (.clang-tidy) over each source with the flags the build uses, against Xcode's SDK.
check-tidy: check-llvm
	@$(CLANG_TIDY) --quiet smith.c -- $(CFLAGS) -isysroot $$(xcrun --show-sdk-path)
	@$(CLANG_TIDY) --quiet tests/stub.c -- $(CFLAGS) -DNAME='"stub"' -isysroot $$(xcrun --show-sdk-path)

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
