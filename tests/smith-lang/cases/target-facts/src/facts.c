// What C's preprocessor says of the target: the answers `target::` must give (tests/smith-lang/cases/target-facts).
#include <float.h>

int c_arch(void) {
#if defined(__aarch64__)
    return 1;
#elif defined(__x86_64__)
    return 2;
#else
    return 0;
#endif
}

int c_os(void) {
#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
    return 1;
#elif defined(__linux__)
    return 2;
#else
    return 0;
#endif
}

int c_address_sanitizer(void) {
#if __has_feature(address_sanitizer)
    return 1;
#else
    return 0;
#endif
}

int c_undefined_sanitizer(void) {
#if __has_feature(undefined_behavior_sanitizer)
    return 1;
#else
    return 0;
#endif
}

int c_thread_sanitizer(void) {
#if __has_feature(thread_sanitizer)
    return 1;
#else
    return 0;
#endif
}

int c_assertions(void) {
#ifdef NDEBUG
    return 0;
#else
    return 1;
#endif
}

int c_float_radix(void) {
    return FLT_RADIX;
}

int c_f32_mantissa_bits(void) {
    return FLT_MANT_DIG;
}

int c_f64_mantissa_bits(void) {
    return DBL_MANT_DIG;
}

int c_float_eval_method(void) {
    return FLT_EVAL_METHOD;
}

int c_static_analyzer(void) {
#ifdef __clang_analyzer__
    return 1;
#else
    return 0;
#endif
}
