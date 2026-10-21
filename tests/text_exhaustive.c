// Every float, through <smith/text.h>'s parser: run by `sh tests/text.sh smith exhaustive` (smith build
// text-exhaustive). For each of the 2^32 bit patterns that is a finite float, the float is formatted twice, as %.9g
// (the shortest that round-trips, which the fast path handles) and as %.17e (18 digits, which only the exact path
// handles), and each text is parsed back. The parser must give strtof's float, in the C locale (this program never sets
// a locale), and for %.9g the original float, bit for bit. Threads split the patterns by stride. Prints the counts;
// exits 1 on any mismatch.
#include <smith/text.h>

#include <float.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

constexpr size_t exhaustive_threads_max = 64;
constexpr uint64_t exhaustive_patterns = (uint64_t)UINT32_MAX + 1;
constexpr uint64_t exhaustive_report_max = 10; // mismatches printed per thread

typedef struct {
    uint64_t first;
    uint64_t stride;
    uint64_t finite;
    uint64_t mismatches;
} Work;

static bool exhaustive_agrees(const char *text, float expected_from_strtof, float original, bool compare_original) {
    float parsed = 0;
    if (!smith_text_parse_f32(smith_text_make(text), &parsed)) return false;
    uint32_t parsed_bits = __builtin_bit_cast(uint32_t, parsed);
    if (parsed_bits != __builtin_bit_cast(uint32_t, expected_from_strtof)) return false;
    return !compare_original || parsed_bits == __builtin_bit_cast(uint32_t, original);
}

static void *exhaustive_run(void *argument) {
    Work *work = argument;
    char text[64];
    for (uint64_t pattern = work->first; pattern < exhaustive_patterns; pattern += work->stride) {
        float f = __builtin_bit_cast(float, (uint32_t)pattern);
        if (f != f || f > FLT_MAX || f < -FLT_MAX) continue;
        work->finite += 1;
        for (int format = 0; format < 2; format++) {
            snprintf(text, sizeof text, format == 0 ? "%.9g" : "%.17e", (double)f);
            bool good = exhaustive_agrees(text, strtof(text, nullptr), f, format == 0);
            if (good) continue;
            work->mismatches += 1;
            if (work->mismatches <= exhaustive_report_max) printf("MISMATCH 0x%08" PRIx64 " \"%s\"\n", pattern, text);
        }
    }
    return nullptr;
}

int main(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    size_t threads = cores < 1 ? 1 : (size_t)cores;
    if (threads > exhaustive_threads_max) threads = exhaustive_threads_max;
    pthread_t handles[exhaustive_threads_max];
    Work work[exhaustive_threads_max];
    for (size_t t = 0; t < threads; t++) {
        work[t] = (Work){.first = t, .stride = threads};
        if (pthread_create(&handles[t], nullptr, exhaustive_run, &work[t]) != 0) return 2;
    }
    uint64_t finite = 0;
    uint64_t mismatches = 0;
    for (size_t t = 0; t < threads; t++) {
        pthread_join(handles[t], nullptr);
        finite += work[t].finite;
        mismatches += work[t].mismatches;
    }
    printf("text: %" PRIu64 " patterns, %" PRIu64 " finite floats, %" PRIu64 " mismatches\n", exhaustive_patterns,
           finite, mismatches);
    return mismatches == 0 ? 0 : 1;
}
