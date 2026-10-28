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
static constexpr Bool shape_wide = true;
#if (1)

static constexpr __INT32_TYPE__ shape_columns = 80;
__INT32_TYPE__ shape_width(void) __asm__("_shape::width");
__INT32_TYPE__ shape_width(void) {
return shape_columns;
}
#else

__INT32_TYPE__ shape_width(void) __asm__("_shape::width");
__INT32_TYPE__ shape_width(void) {
return undeclared_function();
}
#endif
#if SMITH_TARGET_OS == 1 || SMITH_TARGET_OS == 2

__INT32_TYPE__ shape_platform(void) __asm__("_shape::platform");
__INT32_TYPE__ shape_platform(void) {
return 1;
}
#elif SMITH_TARGET_ARCH == 1

__INT32_TYPE__ shape_platform(void) __asm__("_shape::platform");
__INT32_TYPE__ shape_platform(void) {
return 2;
}
#else

__INT32_TYPE__ shape_platform(void) __asm__("_shape::platform");
__INT32_TYPE__ shape_platform(void) {
return undeclared_function();
}
#endif
#if SMITH_TARGET_OS == 1

__INT32_TYPE__ shape_sides(void);
__INT32_TYPE__ shape_sides(void) {
return 4;
}
#else

__INT32_TYPE__ shape_sides(void);
__INT32_TYPE__ shape_sides(void) {
return 4;
}
#endif

__INT32_TYPE__ shape_both(void) __asm__("_shape::both");
__INT32_TYPE__ shape_both(void) {
__INT32_TYPE__ sum = 0;
#if (1)
 {
__INT32_TYPE__ extra = 1;
sum = shape_width() + extra;
}
#endif

return sum + shape_platform();
}
