// Every float, through <smith/text.h>'s parser: run by `sh tests/text.sh smith exhaustive` (smith build
// text-exhaustive). For each of the 2^32 bit patterns that is a finite float, the float is formatted twice, as %.9g
// (the shortest that round-trips, which the fast path handles) and as %.17e (18 digits, which only the exact path
// handles), and each text is parsed back. The parser must give strtof's float, in the C locale (this program never sets
// a locale), and for %.9g the original float, bit for bit. Threads split the patterns by stride. Prints the counts;
// exits 1 on any mismatch.
//
// Then a sample of the doubles, 2^24 patterns spread over the whole range by an odd stride (every 2^40th, so every
// exponent and many mantissas), each formatted as %.17g (which smith_text_parse_f64 must read back to the same bits)
// and as %.25e (26 digits, past what the fast path takes), and each parsed against strtod; and each written by
// smith_text_write_f64 and read back by both parsers.
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

constexpr uint64_t exhaustive_double_samples = (uint64_t)1 << 24;
constexpr uint64_t exhaustive_double_stride = 0x0000010000000001ULL; // odd; 2^24 of them spread over the 2^64 patterns

static bool exhaustive_agrees_f64(const char *text, double original, bool compare_original) {
    double expected = strtod(text, nullptr);
    double parsed = 0;
    if (!smith_text_parse_f64(smith_text_make(text), &parsed)) return false;
    uint64_t parsed_bits = __builtin_bit_cast(uint64_t, parsed);
    if (parsed_bits != __builtin_bit_cast(uint64_t, expected)) return false;
    return !compare_original || parsed_bits == __builtin_bit_cast(uint64_t, original);
}

static bool exhaustive_written(double d) {
    char out[smith_text_f64_capacity + 1];
    size_t length = smith_text_write_f64(out, d);
    out[length] = '\0';
    return exhaustive_agrees_f64(out, d, true);
}

static void *exhaustive_run_f64(void *argument) {
    Work *work = argument;
    char text[128];
    for (uint64_t i = work->first; i < exhaustive_double_samples; i += work->stride) {
        uint64_t pattern = i;
        pattern *= exhaustive_double_stride;
        double d = __builtin_bit_cast(double, pattern);
        if (d != d || d > DBL_MAX || d < -DBL_MAX) continue;
        work->finite += 1;
        bool good = exhaustive_written(d);
        snprintf(text, sizeof text, "%.17g", d);
        good = exhaustive_agrees_f64(text, d, true) && good;
        snprintf(text, sizeof text, "%.25e", d);
        good = exhaustive_agrees_f64(text, d, false) && good;
        if (good) continue;
        work->mismatches += 1;
        if (work->mismatches <= exhaustive_report_max) printf("MISMATCH 0x%016" PRIx64 " \"%s\"\n", pattern, text);
    }
    return nullptr;
}

// Runs `run` on every core and adds up what the threads found.
static bool exhaustive_pass(void *(*run)(void *), uint64_t *finite, uint64_t *mismatches) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    size_t threads = cores < 1 ? 1 : (size_t)cores;
    if (threads > exhaustive_threads_max) threads = exhaustive_threads_max;
    pthread_t handles[exhaustive_threads_max];
    Work work[exhaustive_threads_max];
    for (size_t t = 0; t < threads; t++) {
        work[t] = (Work){.first = t, .stride = threads};
        if (pthread_create(&handles[t], nullptr, run, &work[t]) != 0) return false;
    }
    for (size_t t = 0; t < threads; t++) {
        pthread_join(handles[t], nullptr);
        *finite += work[t].finite;
        *mismatches += work[t].mismatches;
    }
    return true;
}

int main(void) {
    uint64_t finite = 0;
    uint64_t mismatches = 0;
    if (!exhaustive_pass(exhaustive_run, &finite, &mismatches)) return 2;
    printf("text: %" PRIu64 " patterns, %" PRIu64 " finite floats, %" PRIu64 " mismatches\n", exhaustive_patterns,
           finite, mismatches);
    uint64_t double_finite = 0;
    uint64_t double_mismatches = 0;
    if (!exhaustive_pass(exhaustive_run_f64, &double_finite, &double_mismatches)) return 2;
    printf("text: %" PRIu64 " sampled patterns, %" PRIu64 " finite doubles, %" PRIu64 " mismatches\n",
           exhaustive_double_samples, double_finite, double_mismatches);
    return mismatches + double_mismatches == 0 ? 0 : 1;
}
