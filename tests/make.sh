#!/bin/sh
# Tests of the translation of a Makefile into a build.c (issue #44, runtime/make*.c): `smith build --dump-config` in a
# folder with a Makefile and no build file. Each case is a small project with one Makefile for a construct: the
# translation is checked for what it says (named arrays, comments, the call of smith_object, smith_program, smith_step,
# smith_task or smith_alias), the build.c is built with `smith build`, and what it makes is compared with what
# make makes from the same Makefile: the program prints the values the Makefile computed, a file holds what a recipe
# wrote, and the compile and link commands are the same as `make -n -B` lists (the compiler's name, the dependency flags
# and the order of the sets aside). Needs make. Usage: sh tests/make.sh path/to/smith
set -u
case $1 in /*) smith=$1 ;; *) smith=$(pwd)/$1 ;; esac
work=$(mktemp -d)
trap '[ -n "${KEEP_WORK:-}" ] || rm -rf "$work"' EXIT INT TERM
export XDG_CACHE_HOME=$work/xdg-cache # the module and package caches of the tests, never the real ones

if ! command -v make >/dev/null 2>&1; then
    echo "skipped: no make"
    exit 0
fi

# build.c names smith-cc and smith-c++, so the tests have those names: links to the smith under test.
mkdir "$work/bin"
ln -s "$smith" "$work/bin/smith-cc"
ln -s "$smith" "$work/bin/smith-c++"
PATH=$work/bin:$PATH
export PATH
unset CC CXX MAKEFLAGS MFLAGS

failures=0
total=0
cases=0
out=
err=
rc=0

capture() {
    out=$("$@" 2>"$work/err")
    rc=$?
    err=$(cat "$work/err")
}

expect() { # name, expected, actual
    total=$((total + 1))
    if [ "$2" = "$3" ]; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

# Whether the build.c of the case holds the text (a fixed string).
has() { # name, text
    total=$((total + 1))
    if grep -F -q -- "$2" build.c; then
        echo "ok    $1"
    else
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  build.c lacks: $2"
        sed 's/^/    | /' build.c | head -60
    fi
}

lacks() { # name, text
    total=$((total + 1))
    if grep -F -q -- "$2" build.c; then
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  build.c has: $2"
    else
        echo "ok    $1"
    fi
}

contains() { # name, text, needle
    total=$((total + 1))
    case $2 in
    *"$3"*) echo "ok    $1" ;;
    *)
        failures=$((failures + 1))
        echo "FAIL  $1"
        echo "  expected to contain: $3"
        echo "  actual:              $2"
        ;;
    esac
}

# A fresh project folder.
project() {
    cases=$((cases + 1))
    mkdir "$work/$cases"
    cd "$work/$cases" || exit 1
}

# The translation: build.c, and what was said on standard error.
translate() {
    "$smith" build --dump-config >build.c 2>dump.err
    rc=$?
    [ "$rc" -eq 0 ] && tidy_check
}

# The build.c a translation wrote passes `smith lint` and `smith fmt --dry-run` with smith's defaults (a project with no
# .clang-tidy or .clang-format of its own): names in lower case, functions of at most 70 lines, formatted.
tidy_check() {
    "$smith" lint >lint.log 2>&1
    findings=$(grep -E '^build\.c:[0-9]+:[0-9]+: (warning|error)' lint.log)
    formatting=$("$smith" fmt --dry-run build.c 2>&1 | grep -c 'clang-format-violations')
    expect "translation $cases: build.c is lint-clean and format-clean" "none 0" "${findings:-none} $formatting"
}

# main.c prints R1 .. R<count> (strings, or "unset"), which the Makefile gives as -DRn='"value"'.
printer() { # count
    {
        echo '#include <stdio.h>'
        i=1
        while [ "$i" -le "$1" ]; do
            printf '#ifndef R%s\n#define R%s "unset"\n#endif\n' "$i" "$i"
            i=$((i + 1))
        done
        echo 'int main(void) {'
        i=1
        while [ "$i" -le "$1" ]; do
            printf '    puts("R%s=" R%s);\n' "$i" "$i"
            i=$((i + 1))
        done
        echo '    return 0;'
        echo '}'
    } >main.c
}

# Builds the program with make, then with the translation, and compares what they print.
same_output() { # name, program, [options of both]
    name=$1
    program=$2
    shift 2
    rm -rf build .smith "$program"
    make "$@" >make.log 2>&1
    expect "$name: make builds it" 0 "$?"
    "./$program" >make.out 2>&1
    rm -rf build "$program"
    translate
    expect "$name: the translation exits 0" 0 "$rc"
    options=""
    for option in "$@"; do
        case $option in *=*) options="$options -D$option" ;; esac
    done
    # shellcheck disable=SC2086
    "$smith" build $options >smith.log 2>&1
    status=$?
    expect "$name: smith build builds it" 0 "$status"
    [ "$status" -eq 0 ] || sed 's/^/    | /' smith.log | tail -12
    "./$program" >smith.out 2>&1
    expect "$name: it prints what make's build prints" "$(cat make.out)" "$(cat smith.out)"
}

# The compile and link commands of a command listing, with the compiler called CC and the dependency flags out, each
# as the shell reads it, one per line and sorted.
canonical() {
    sed -E -e 's#^([^ ]*/)?(smith-cc|cc|clang|gcc) #CC #' -e 's#^([^ ]*/)?(smith-c\+\+|c\+\+|g\+\+|clang\+\+) #CXX #' \
        -e 's# -MMD -MF [^ ]*\.d # #' -e 's# -MMD # #' -e 's# -MP # #' -e 's#  *# #g' |
        grep '^CC \|^CXX ' |
        while IFS= read -r line; do
            eval "set -- $line" 2>/dev/null || {
                echo "$line"
                continue
            }
            printf '%s' "$1"
            shift
            for word in "$@"; do printf ' %s' "$word"; done
            echo
        done | sort -u
}

# Compares the compile and link commands of `make -n -B` with the translation's for the targets.
same_commands() { # name, targets...
    name=$1
    shift
    make -n -B 2>/dev/null | canonical >make.cmds
    "$smith" build -t commands "$@" 2>/dev/null >smith.raw
    while IFS= read -r line; do
        case $line in
        "sh .smith/command/"*) cat "${line#sh }" ;;
        *) echo "$line" ;;
        esac
    done <smith.raw | canonical >smith.cmds
    expect "$name: the same compile and link commands as make" "$(cat make.cmds)" "$(cat smith.cmds)"
    contains "$name: and some" "$(cat smith.cmds)" "CC"
}

# 1. Variables: their flavors, expansion, += and ?=, substitution references, ${X}, override.
project
printer 12
cat >Makefile <<'MK'
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
MK
translate
same_output "variables" prog
has "variables: the flags of the compile are written out" '"-DR1=\"2\""'
has "variables: a command-line variable is an option" 'smith_option_value(b, "UNDEFINED_VARIABLE"'
has "variables: the option has the Makefile's line" 'Makefile:'
same_output "variables: C and D set on the command line" prog C=cmd D=cmd2

# 2. The functions of the subset.
project
printer 20
cat >Makefile <<'MK'
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
MK
translate
same_output "functions" prog
expect "functions: no construct is reported" "" "$(cat dump.err)"

# 3. Rules: explicit and pattern rules, order-only prerequisites, the automatic variables, a recipe that is not a compile.
project
printer 1
echo one >a.in
echo two >b.in
cat >Makefile <<'MK'
all: prog both.txt a.txt b.txt
prog: main.o
	cc $^ -o $@
%.o: %.c | outdir
	cc -c $< -o $@
%.txt: %.in
	echo "$* $< $@" > $@
both.txt: a.in b.in
	echo "$@|$<|$^|$?" > $@
outdir:
	mkdir -p $@
.PHONY: outdir
MK
translate
rm -f main.o
same_output "rules" prog
rm -f both.txt a.txt b.txt main.o
make >/dev/null 2>&1
cat both.txt a.txt b.txt >make.files
rm -f both.txt a.txt b.txt main.o prog
"$smith" build >/dev/null 2>&1
cat both.txt a.txt b.txt >smith.files 2>&1
expect "rules: automatic variables and the stem in recipes" "$(cat make.files)" "$(cat smith.files)"
has "rules: a pattern rule's compile is a smith_object" 'smith_object(b, &object_1);'
has "rules: a link is a smith_program" 'smith_program(b, &program_1);'
has "rules: another recipe is a smith_step" 'smith_step(b, &step_'
has "rules: each block says where it came from" '// Makefile:'

# 4. Recipes: @ and -, line continuations, several lines.
project
cat >Makefile <<'MK'
all: out.txt
out.txt:
	@echo first > $@
	-false
	echo second \
		third >> $@
	@echo 'quoted "text"' >> $@
MK
translate
make >/dev/null 2>&1
cp out.txt make.out
rm out.txt
"$smith" build >/dev/null 2>&1
expect "recipes: the lines run in order, - ignores a failure, a continuation joins" "$(cat make.out)" "$(cat out.txt)"
has "recipes: several lines are one script" 'set -e'

# 5. Phony targets, the default goal, and the names of smith's verbs.
project
cat >Makefile <<'MK'
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
MK
printer 1
translate
has "phony: the first target is the default" 'smith_default(b, (const char *[]){"prog", nullptr});'
has "phony: clean is dropped, with the reason" '`clean` is dropped'
has "phony: a verb's name becomes make-test, which smith test runs" 'smith_task(b, "make-test", &'
has "phony: and it is one of the tests" '.test = true,'
has "phony: run is smith run" '`run` is `smith run`'
has "phony: check is an alias of another name" 'smith_alias(b, "make-check", '
has "phony: another phony target is a task" 'smith_task(b, "extra", &'
lacks "phony: no task named after a verb" 'smith_task(b, "test"'
"$smith" build >/dev/null 2>&1
expect "phony: the default builds the program" ok "$([ -x prog ] && echo ok)"
"$smith" build extra >/dev/null 2>&1
expect "phony: smith build extra runs the task" extra "$(cat extra.txt)"
cat >Makefile <<'MK'
.DEFAULT_GOAL := second
first:
	echo first
second:
	echo second
MK
translate
has ".DEFAULT_GOAL names the default" 'smith_default(b, (const char *[]){"second", nullptr});'

# 6. include and -include.
project
printer 3
cat >vars.mk <<'MK'
FROM_INCLUDE = included
MK
cat >Makefile <<'MK'
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
MK
translate
rm -f main.o
same_output "include" prog
expect "include: nothing is reported" "" "$(cat dump.err)"
has "include: dependency files are dropped, with a note" 'smith tracks the headers'
lacks "include: -MMD is not kept" '"-MMD"'

# 7. Conditionals: ifeq, ifneq, ifdef, ifndef, else, else ifeq.
project
printer 8
cat >Makefile <<'MK'
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
MK
translate
same_output "conditionals" prog
has "conditionals: a test of an option the Makefile never sets is an option" 'build_never'

# 8. A test of the platform keeps both branches, as an if on b->os and b->arch.
project
printer 5
cat >Makefile <<'MK'
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
MK
translate
same_output "platform" prog
has "platform: Darwin is a test of b->os" 'strcmp(b->os, "Darwin") == 0'
has "platform: and Linux" 'strcmp(b->os, "Linux") == 0'
has "platform: OS == Windows_NT is a test of b->os too" 'strcmp(b->os, "Windows_NT") == 0'
has "platform: the architecture is b->arch" 'b->arch'
expect "platform: a test of the platform is not reported as unsupported" "" "$(cat dump.err)"

# 9. Command-line knobs are -D options of the build file.
project
printer 3
cat >Makefile <<'MK'
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
MK
translate
has "knobs: the first line names the options" '// Options the Makefile reads from the command line'
has "knobs: DEBUG" '-DDEBUG=...'
has "knobs: VERBOSE" '-DVERBOSE=...'
same_output "knobs: the defaults" prog
same_output "knobs: DEBUG=1" prog DEBUG=1
same_output "knobs: DEBUG=1 VERBOSE=yes EXTRA=more" prog DEBUG=1 VERBOSE=yes EXTRA=more

# 10. make's built-in rules for C: a compile and a link nobody wrote.
project
printer 2
mv main.c prog.c
printf 'int helper(void) { return 1; }\n' >helper.c
sed -i.bak 's/int main(void) {/int helper(void);\nint main(void) {/' prog.c
cat >Makefile <<'MK'
CFLAGS = -DR1='"built-in"' -O1
prog: prog.o helper.o
MK
translate
same_output "built-in rules" prog
has "built-in rules: the compile is a smith_object" 'smith_object(b, &object_1);'
has "built-in rules: with the CFLAGS" '"-O1"'
has "built-in rules: the link is a smith_program" 'smith_program(b, &program_1);'

# 11. An archive is a step, and the program that links it names it a library.
project
printer 1
printf 'const char *name(void) { return "lib"; }\n' >lib.c
cat >Makefile <<'MK'
all: prog
libx.a: lib.o
	ar rcs $@ $^
lib.o: lib.c
	cc -c lib.c -o lib.o
prog: main.o libx.a
	cc main.o libx.a -o prog
main.o: main.c
	cc -DR1='"archive"' -c main.c -o main.o
MK
translate
same_output "archive" prog
has "archive: ar is a step" '"ar", "rcs"'
has "archive: the library of the link" '.libraries = (const char *[]){"libx.a", nullptr}'

# 12. Recursive make: a goal of the same Makefile, and -C another folder.
project
printer 1
mkdir sub
printf 'const char *name(void) { return "sub"; }\n' >sub/lib.c
cat >sub/Makefile <<'MK'
CFLAGS = -O1
all: lib.a
lib.a: lib.o
	ar rcs $@ $^
lib.o: lib.c
	cc $(CFLAGS) -c lib.c -o lib.o
MK
cat >Makefile <<'MK'
all:
	$(MAKE) prog
prog: main.c sub/lib.a
	cc -DR1='"recursive"' main.c sub/lib.a -o prog
sub/lib.a:
	$(MAKE) -C sub
MK
translate
rm -f sub/lib.a sub/lib.o
same_output "recursive make" prog
rm -f sub/lib.a sub/lib.o
has "recursive make: the other folder's targets are named under it" '"sub/lib.a"'
has "recursive make: and run in it" "cd 'sub'"
has "recursive make: the other Makefile is the source of the comment" 'sub/Makefile:'
expect "recursive make: nothing is reported" "" "$(cat dump.err)"

# 13. What is outside the subset is reported by name, file and line, and the rest still translates.
project
printer 1
echo other >other
cat >Makefile <<'MK'
VPATH = src
.SECONDEXPANSION:
prog: main.c
	cc -DR1='"x"' main.c -o prog
prog:: other
	echo double
%.o: CFLAGS = -O3
$(eval A = 1)
all:
	$(eval B = 2)
out := $(shell echo frozen)
MK
translate
expect "reported: the translation still exits 0" 0 "$rc"
contains "reported: VPATH by line" "$(cat dump.err)" "Makefile:1: not translated: VPATH is not supported"
contains "reported: .SECONDEXPANSION" "$(cat dump.err)" "Makefile:2: not translated: .SECONDEXPANSION is not supported"
contains "reported: a double-colon rule" "$(cat dump.err)" "Makefile:5: not translated: a double-colon rule is not supported"
contains "reported: a target-specific variable" "$(cat dump.err)" "Makefile:7: not translated: a target-specific variable is not supported"
contains "reported: \$(shell) is frozen" "$(cat dump.err)" 'Makefile:11: not translated: $(shell echo frozen) was run now and is frozen'
contains "reported: eval in a recipe" "$(cat dump.err)" 'Makefile:10: not translated: $(eval) in a recipe is not supported'
has "reported: the build.c says so too" '// NOT TRANSLATED'
has "reported: with the line" '//   Makefile:1: VPATH is not supported'
"$smith" build >/dev/null 2>&1
expect "reported: and the build.c builds" 0 "$?"

# 14. The same commands as make for a small project with a flags variable and a pattern rule.
project
printer 1
cp main.c second.c
sed -i.bak 's/int main(void) {/int second_main(void) {/' second.c
cat >Makefile <<'MK'
WARN = -Wall -Wextra
CFLAGS = $(WARN) -O1 -DR1='"commands"'
OBJ = main.o other.o
prog: $(OBJ)
	cc $(OBJ) -o prog
%.o: %.c
	cc $(CFLAGS) -c $< -o $@
other.o: second.c
	cc $(CFLAGS) -DSPECIAL -c second.c -o other.o
MK
translate
"$smith" build >/dev/null 2>&1
same_commands "commands" prog
has "readable: a flags variable is an array with the Makefile's name" 'const char *const *build_warn ='
has "readable: and the other uses it" 'smith_cat(b, build_warn, '
has "readable: the compiler is smith-cc" '.compiler = "smith-cc"'
has "readable: the header names the Makefile" '// build.c, translated from Makefile'

# 15. A rule under a test of the platform is a step that build.c declares only there; two that differ are reported.
project
cat >Makefile <<'MK'
all: here.txt
ifneq ($(shell uname -s),NotAnOs)
here.txt:
	echo here > $@
endif
ifeq ($(shell uname -s),NotAnOs)
other.txt:
	echo one > $@
else
other.txt:
	echo two > $@
endif
MK
translate
has "guarded rule: the step is declared under the test" 'if ((strcmp(b->os, "NotAnOs") != 0)) {'
contains "guarded rule: two recipes under different tests are reported" "$(cat dump.err)" "other.txt has recipes under different tests"
"$smith" build >/dev/null 2>&1
expect "guarded rule: and the build.c builds it" here "$(cat here.txt 2>/dev/null)"

# A wildcard of a command-line variable is read at its default, and said so.
project
echo font >font.bdf
cat >Makefile <<'MK'
FONT ?= font.bdf
FILES = $(wildcard $(FONT))
out.txt: $(FILES)
	cat $(FILES) > $@
MK
translate
contains "wildcard of an option: reported" "$(cat dump.err)" '$(wildcard $(FONT)) was read now, with the option at its default (font.bdf)'
has "wildcard of an option: its default is the file" '"font.bdf"'
"$smith" build >/dev/null 2>&1
expect "wildcard of an option: the build uses the file" font "$(cat out.txt 2>/dev/null)"

# 16. Which Makefile, and when.
project
cat >Makefile <<'MK'
a:
	echo makefile
MK
cat >GNUmakefile <<'MK'
b:
	echo gnumakefile
MK
translate
has "files: GNUmakefile comes first" '"b"'
lacks "files: and Makefile is left" '"a"'
rm GNUmakefile
translate
has "files: otherwise Makefile" '"a"'
cat >build.c <<'C'
#include <smith/build.h>
void build(SmithBuild *b) { (void)b; }
C
"$smith" build --dump-config >dumped.txt 2>&1
case $(cat dumped.txt) in
*"translated from"*) echo "FAIL  files: a folder with a build.c is not translated"; failures=$((failures + 1)) ;;
*) echo "ok    files: a folder with a build.c is not translated" ;;
esac
total=$((total + 1))

[ -z "${KEEP_WORK:-}" ] || echo "kept: $work"
# 17. A Makefile and a main, and no build file: --dump-config translates the Makefile, and nothing builds by convention.
project
printf 'int main(void) { return 0; }\n' >main.c
cat >Makefile <<'MK'
prog: main.c
	cc main.c -o prog
MK
capture "$smith" build --dump-config
expect "makefile and main: --dump-config exits 0" 0 "$rc"
contains "makefile and main: it translates the Makefile" "$out" "translated from Makefile"
capture "$smith" build
expect "makefile and main: smith build stops" 2 "$rc"
contains "makefile and main: and says to translate it" "$err" "smith build --dump-config > build.c"
contains "makefile and main: or to delete it" "$err" "delete it to build by convention"
capture "$smith" run
expect "makefile and main: a verb stops too" 2 "$rc"
contains "makefile and main: with the same message" "$err" "smith build --dump-config > build.c"
expect "makefile and main: nothing was built" "" "$(ls prog build .smith 2>/dev/null)"
rm Makefile
capture "$smith" build --dump-config
contains "no Makefile: the conventions are dumped" "$out" "smith_app"

# 18. An empty build file is none, for build.c and for build.smith, in the translator and in the conventions.
for name in build.c build.smith; do
    project
    printf 'int main(void) { return 0; }\n' >main.c
    printf 'prog: main.c\n\tcc main.c -o prog\n' >Makefile
    : >"$name"
    capture "$smith" build --dump-config
    expect "empty $name: the Makefile is translated" 0 "$rc"
    contains "empty $name: the output is the translation" "$out" "translated from Makefile"
    capture "$smith" build
    contains "empty $name: smith build still sees the Makefile" "$err" "smith build --dump-config > build.c"
    rm Makefile
    capture "$smith" build --dump-config
    contains "empty $name: and with no Makefile the conventions are dumped" "$out" "smith_app"
done

# 19. A Makefile too big for one function of 70 lines: build.c is a function for each run of targets, the variables are
# statics, and it still passes lint and fmt (translate checks) and builds.
project
i=1
: >Makefile
files=""
while [ "$i" -le 45 ]; do
    : >"src$i.dat"
    files="$files src$i.dat"
    i=$((i + 1))
done
{
    echo 'DATA := $(sort $(wildcard src*.dat))'
    echo 'PREFIX ?= out'
    i=1
    while [ "$i" -le 30 ]; do
        printf 'step%s: $(DATA)\n\t@touch $(PREFIX)%s.txt $(DATA)\n' "$i" "$i"
        i=$((i + 1))
    done
    echo '.PHONY: all'
    echo 'all: step1 step30'
} >Makefile
translate
expect "split: the Makefile is translated" 0 "$rc"
has "split: the variables are statics" 'static const char *const *build_data;'
has "split: a long list is written in pieces" 'build_data = smith_cat(b, build_data,'
has "split: a function for each run of targets" 'static void part_2(SmithBuild *b) {'
has "split: build() calls them" '    part_1(b);'
capture "$smith" build -DPREFIX=pre
expect "split: smith build builds it" 0 "$rc"
expect "split: the option reaches a recipe" "pre1.txt" "$(ls pre1.txt 2>/dev/null)"

echo "$((total - failures)) of $total passed"
[ "$failures" -eq 0 ]
