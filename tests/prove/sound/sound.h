// The harness of the soundness tests (issue #64): a function smith prove proved is called on many random inputs that
// satisfy its smith_requires, under AddressSanitizer and UBSan, and a contract that fails while it runs (an ensures, an
// invariant, an assert, a requires of a callee) or a sanitizer's report is a wrong "proved". A harness is
// tests/prove/sound/<case>.c: it includes the case it tests, so static functions are reachable, and is run by
// tests/prove_sound_test.smith, which first asks smith prove for `ok` on each function the harness lists on its `// proves:` line.
//
// Inputs are generated, then filtered: a failed smith_requires of the function is not an error but a rejected input
// (the hook below turns every failed contract into a longjmp), counted, and a harness whose accepted inputs are rare
// fails as vacuous. Aliased pointer arguments are passed where the types allow. The values are biased toward the
// boundaries (zero, one, the extremes of the type, the edges of what the contract allows) by the generators below.
#pragma once
#include <smith/contract.h>
#include <smith/trace.h>

#include <limits.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t state;
    jmp_buf jump;
    SmithFailureKind kind;
    unsigned long iterations;
    unsigned long accepted;
    unsigned long rejected;
    char note[256];
    const char *name;
    bool running;
} Sound;

static Sound sound;

static uint64_t sound_next(void) { // splitmix64
    uint64_t z = (sound.state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// A value below n (n > 0), the edges (0, 1, n - 1) more often than chance would.
static uint64_t sound_below(uint64_t n) {
    uint64_t r = sound_next();
    switch (r & 7) {
    case 0: return 0;
    case 1: return n > 1 ? 1 : 0;
    case 2: return n - 1;
    case 3: return n > 1 ? n - 2 : 0;
    default: return (r >> 3) % n;
    }
}

// A value in [low, high], the edges and their neighbours more often than chance would.
static int64_t sound_between(int64_t low, int64_t high) {
    const uint64_t span = (uint64_t)high - (uint64_t)low;
    if (span == UINT64_MAX) return (int64_t)sound_next();
    return (int64_t)((uint64_t)low + sound_below(span + 1));
}

static int32_t sound_i32(void) {
    static const int32_t edges[] = {0, 1, -1, 2, -2, INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1, 100, 1000};
    const uint64_t r = sound_next();
    if ((r & 3) == 0) return edges[(r >> 2) % (sizeof edges / sizeof edges[0])];
    if ((r & 3) == 1) return (int32_t)((r >> 2) % 2001) - 1000;
    return (int32_t)(r >> 8);
}

static uint32_t sound_u32(void) {
    static const uint32_t edges[] = {0, 1, 2, UINT32_MAX, UINT32_MAX - 1, 255, 256, 1u << 31, (1u << 31) - 1};
    const uint64_t r = sound_next();
    if ((r & 3) == 0) return edges[(r >> 2) % (sizeof edges / sizeof edges[0])];
    return (uint32_t)(r >> 8);
}

static uint8_t sound_u8(void) {
    const uint64_t r = sound_next();
    return (r & 7) == 0 ? (uint8_t)(r >> 8 & 1 ? 255 : 0) : (uint8_t)(r >> 8);
}

static uint64_t sound_u64(void) {
    static const uint64_t edges[] = {0, 1, 2, UINT64_MAX, UINT64_MAX - 1, 1ULL << 63, (1ULL << 63) - 1, 10, 99, 100};
    const uint64_t r = sound_next();
    if ((r & 3) == 0) return edges[(r >> 2) % (sizeof edges / sizeof edges[0])];
    return sound_next() >> ((r >> 8) % 64); // every bit length
}

static void sound_fill(void *memory, size_t size) {
    uint8_t *bytes = memory;
    for (size_t i = 0; i < size; i++)
        bytes[i] = sound_u8();
}

// What the failing input was, printed when the run dies by a failed contract, a sanitizer's report or an abort.
#define SOUND_NOTE(...) (void)snprintf(sound.note, sizeof sound.note, __VA_ARGS__)

static void sound_hook(SmithFailureKind kind, const uintptr_t *frames, size_t count) {
    (void)frames;
    (void)count;
    sound.kind = kind;
    longjmp(sound.jump, 1);
}

static void sound_death(void) {
    if (sound.running) fprintf(stderr, "sound: %s died on iteration %lu, input: %s\n", sound.name, sound.iterations, sound.note);
}

void __sanitizer_set_death_callback(void (*callback)(void));

static void sound_setup(const char *argv0, int argc, char **argv) {
    (void)argv0;
    sound.state = argc > 1 ? strtoull(argv[1], nullptr, 10) : 1;
    smith_failure_hook = sound_hook;
    __sanitizer_set_death_callback(sound_death);
}

// Runs `call` (statements that build an input, SOUND_NOTE it and call the function) `count` times. A failed requires
// (the input does not satisfy it) is rejected; any other failed contract is a failure of the proof. At least a
// tenth of the inputs must be accepted, or the test proves nothing.
#define SOUND_RUN(label, count, ...)                                                                                   \
    do {                                                                                                               \
        sound.name = (label);                                                                                          \
        sound.accepted = sound.rejected = 0;                                                                           \
        sound.running = true;                                                                                          \
        for (sound.iterations = 0; sound.iterations < (unsigned long)(count); sound.iterations++) {                    \
            if (setjmp(sound.jump) == 0) {                                                                             \
                __VA_ARGS__;                                                                                           \
                sound.accepted++;                                                                                      \
            } else if (sound.kind == SmithFailure_Requires) {                                                          \
                sound.rejected++;                                                                                      \
            } else {                                                                                                   \
                fprintf(stderr, "sound: %s: %s failed on iteration %lu, input: %s\n", (label),                         \
                        smith_failure_heading(sound.kind), sound.iterations, sound.note);                              \
                exit(1);                                                                                               \
            }                                                                                                          \
        }                                                                                                              \
        sound.running = false;                                                                                         \
        if (sound.accepted * 10 < sound.iterations) {                                                                  \
            fprintf(stderr, "sound: %s: vacuous, %lu of %lu inputs satisfied the requires\n", (label), sound.accepted, \
                    sound.iterations);                                                                                 \
            exit(1);                                                                                                   \
        }                                                                                                              \
        printf("ok    %s (%lu inputs, %lu rejected)\n", (label), sound.accepted, sound.rejected);                      \
    } while (0)

// The same as SOUND_RUN with the iterations the run asks for (argv[2], 2000 by default).
static unsigned long sound_count = 2000;
#define SOUND_MAIN(...)                                                                                                \
    int main(int argc, char **argv) {                                                                                  \
        sound_setup(argv[0], argc, argv);                                                                              \
        if (argc > 2) sound_count = strtoul(argv[2], nullptr, 10);                                                     \
        __VA_ARGS__                                                                                                    \
        return 0;                                                                                                      \
    }
