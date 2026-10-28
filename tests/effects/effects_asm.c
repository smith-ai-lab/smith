// Inline assembly is read by LLVM's assembler (issue #396): a block with no system call and no call or jump out of it
// has no effect; a system call has the effects of the libc function its number names; an unknown number, a call
// through a register are `c`. AArch64 (macOS and Linux), where the number is in
// x16 (macOS) or x8 (Linux); the same lines on both.
#if defined(__aarch64__)
#if defined(__APPLE__)
#define NUMBER_REGISTER "x16"
#define SYS_WRITE_NUMBER 4
#define SYMBOL(name) "_" #name
#else
#define NUMBER_REGISTER "x8"
#define SYS_WRITE_NUMBER 64
#define SYMBOL(name) #name
#endif

typedef int Vector __attribute__((vector_size(16)));

[[std::pure]] Vector simd_add(Vector a, Vector b) {
    Vector sum;
    asm("add %0.4s, %1.4s, %2.4s" : "=w"(sum) : "w"(a), "w"(b));
    return sum;
}

[[std::pure]] unsigned long counter(void) {
    unsigned long ticks;
    asm volatile("mrs %0, cntvct_el0" : "=r"(ticks));
    return ticks;
}

[[std::pure]] void barrier(void) {
    asm volatile("dmb ish" ::: "memory");
}

[[std::pure]] void compiler_barrier(void) {
    asm volatile("" ::: "memory");
}

// The number is a constant in a register variable.
[[std::pure]] long raw_write(int fd, const char *text, unsigned long count) {
    register long number asm(NUMBER_REGISTER) = SYS_WRITE_NUMBER;
    register long a0 asm("x0") = fd;
    register long a1 asm("x1") = (long)text;
    register long a2 asm("x2") = (long)count;
    asm volatile("svc #0" : "+r"(a0) : "r"(number), "r"(a1), "r"(a2) : "memory");
    return a0;
}

// The number is loaded by the block from an immediate operand.
[[std::pure]] long raw_write_immediate(int fd, const char *text, unsigned long count) {
    register long a0 asm("x0") = fd;
    register long a1 asm("x1") = (long)text;
    register long a2 asm("x2") = (long)count;
    asm volatile("mov " NUMBER_REGISTER ", %3\n\tsvc #0"
                 : "+r"(a0)
                 : "r"(a1), "r"(a2), "i"(SYS_WRITE_NUMBER)
                 : "memory");
    return a0;
}

// The same call, allowed: write is files and net in the table.
[[std::effects(files, net)]] long raw_write_declared(int fd, const char *text, unsigned long count) {
    register long number asm(NUMBER_REGISTER) = SYS_WRITE_NUMBER;
    register long a0 asm("x0") = fd;
    register long a1 asm("x1") = (long)text;
    register long a2 asm("x2") = (long)count;
    asm volatile("svc #0" : "+r"(a0) : "r"(number), "r"(a1), "r"(a2) : "memory");
    return a0;
}

// A number only known at run time.
[[std::pure]] long raw_unknown(long which) {
    register long number asm(NUMBER_REGISTER) = which;
    register long a0 asm("x0") = 0;
    asm volatile("svc #0" : "+r"(a0) : "r"(number) : "memory");
    return a0;
}

// A call out of the block to a named symbol takes its effects.
[[std::pure]] void calls_puts(const char *text) {
    register long a0 asm("x0") = (long)text;
    asm volatile("bl " SYMBOL(puts) : "+r"(a0) : : "x30", "memory");
}

// A call through a register is `c`.
[[std::pure]] void calls_register(void (*function)(void)) {
    asm volatile("blr %0" : : "r"(function) : "x30", "memory");
}
#endif
