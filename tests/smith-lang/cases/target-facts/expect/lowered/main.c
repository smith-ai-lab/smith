#ifndef SMITH_TARGET_ARCH
#if defined(__aarch64__)
#define SMITH_TARGET_ARCH 1
#elif defined(__x86_64__)
#define SMITH_TARGET_ARCH 2
#else
#define SMITH_TARGET_ARCH 0
#endif
#endif
#ifndef SMITH_TARGET_OS
#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#define SMITH_TARGET_OS 1
#elif defined(__linux__)
#define SMITH_TARGET_OS 2
#else
#define SMITH_TARGET_OS 0
#endif
#endif
#ifndef SMITH_TARGET_ADDRESS_SANITIZER
#if __has_feature(address_sanitizer)
#define SMITH_TARGET_ADDRESS_SANITIZER 1
#else
#define SMITH_TARGET_ADDRESS_SANITIZER 0
#endif
#endif
#ifndef SMITH_TARGET_UNDEFINED_SANITIZER
#if __has_feature(undefined_behavior_sanitizer)
#define SMITH_TARGET_UNDEFINED_SANITIZER 1
#else
#define SMITH_TARGET_UNDEFINED_SANITIZER 0
#endif
#endif
#ifndef SMITH_TARGET_THREAD_SANITIZER
#if __has_feature(thread_sanitizer)
#define SMITH_TARGET_THREAD_SANITIZER 1
#else
#define SMITH_TARGET_THREAD_SANITIZER 0
#endif
#endif
#ifndef SMITH_TARGET_ASSERTIONS
#ifdef NDEBUG
#define SMITH_TARGET_ASSERTIONS 0
#else
#define SMITH_TARGET_ASSERTIONS 1
#endif
#endif
#ifndef SMITH_TARGET_FLOAT_RADIX
#define SMITH_TARGET_FLOAT_RADIX __FLT_RADIX__
#endif
#ifndef SMITH_TARGET_F32_MANTISSA_BITS
#define SMITH_TARGET_F32_MANTISSA_BITS __FLT_MANT_DIG__
#endif
#ifndef SMITH_TARGET_F64_MANTISSA_BITS
#define SMITH_TARGET_F64_MANTISSA_BITS __DBL_MANT_DIG__
#endif
#ifndef SMITH_TARGET_FLOAT_EVAL_METHOD
#define SMITH_TARGET_FLOAT_EVAL_METHOD __FLT_EVAL_METHOD__
#endif
#ifndef SMITH_TARGET_STATIC_ANALYZER
#ifdef __clang_analyzer__
#define SMITH_TARGET_STATIC_ANALYZER 1
#else
#define SMITH_TARGET_STATIC_ANALYZER 0
#endif
#endif
#include <stdio.h>

extern __INT32_TYPE__ c_arch(void);
extern __INT32_TYPE__ c_os(void);
extern __INT32_TYPE__ c_address_sanitizer(void);
extern __INT32_TYPE__ c_undefined_sanitizer(void);
extern __INT32_TYPE__ c_thread_sanitizer(void);
extern __INT32_TYPE__ c_assertions(void);
extern __INT32_TYPE__ c_float_radix(void);
extern __INT32_TYPE__ c_f32_mantissa_bits(void);
extern __INT32_TYPE__ c_f64_mantissa_bits(void);
extern __INT32_TYPE__ c_float_eval_method(void);
extern __INT32_TYPE__ c_static_analyzer(void);
static_assert(SMITH_TARGET_FLOAT_RADIX == 2);
static_assert(SMITH_TARGET_F32_MANTISSA_BITS == 24 && SMITH_TARGET_F64_MANTISSA_BITS == 53, "f32 and f64 are IEEE");
static_assert(1 != 2 && 1 != 2);
static_assert(SMITH_TARGET_ARCH == 1 || SMITH_TARGET_ARCH == 2 || SMITH_TARGET_ARCH == 0);
__UINT8_TYPE__ mantissa[SMITH_TARGET_F32_MANTISSA_BITS];
constexpr __INT32_TYPE__ arch_copy = SMITH_TARGET_ARCH;
constexpr bool sanitized = SMITH_TARGET_ADDRESS_SANITIZER;
static_assert(_Countof(mantissa) == 24);
static __INT32_TYPE__ check(const char *name, __INT32_TYPE__ smith, __INT32_TYPE__ compiler) {
if (smith != compiler) {
printf("%s: %d, and the compiler says %d\n", name, smith, compiler);
return 1;
}
printf("%s ok\n", name);
return 0;
}
int main(void) {
__INT32_TYPE__ bad = 0;
bad += check("arch", SMITH_TARGET_ARCH, c_arch());
bad += check("os", SMITH_TARGET_OS, c_os());
bad += check("address_sanitizer", SMITH_TARGET_ADDRESS_SANITIZER, c_address_sanitizer());
bad += check("undefined_sanitizer", SMITH_TARGET_UNDEFINED_SANITIZER, c_undefined_sanitizer());
bad += check("thread_sanitizer", SMITH_TARGET_THREAD_SANITIZER, c_thread_sanitizer());
bad += check("assertions", SMITH_TARGET_ASSERTIONS, c_assertions());
bad += check("float_radix", SMITH_TARGET_FLOAT_RADIX, c_float_radix());
bad += check("f32_mantissa_bits", SMITH_TARGET_F32_MANTISSA_BITS, c_f32_mantissa_bits());
bad += check("f64_mantissa_bits", SMITH_TARGET_F64_MANTISSA_BITS, c_f64_mantissa_bits());
bad += check("float_eval_method", SMITH_TARGET_FLOAT_EVAL_METHOD, c_float_eval_method());
bad += check("static_analyzer", SMITH_TARGET_STATIC_ANALYZER, c_static_analyzer());
bad += check("constexpr copies", arch_copy + sanitized, c_arch() + c_address_sanitizer());
return bad;
}
