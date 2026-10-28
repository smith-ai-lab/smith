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
#ifndef SMITH_TARGET_FLOAT_RADIX
#define SMITH_TARGET_FLOAT_RADIX __FLT_RADIX__
#endif
static constexpr __INT32_TYPE__ kind_wide = 8;
static constexpr bool kind_both = kind_wide == 8 && SMITH_TARGET_FLOAT_RADIX == 2;
__INT32_TYPE__ kind_arch_name() __asm__("_kind::arch_name");
__INT32_TYPE__ kind_arch_name() {
#if SMITH_TARGET_ARCH == 1
 {
return 1;
}
#elif SMITH_TARGET_ARCH == 2
 {
undeclared_function();
}
#else
 {
return 3;
}
#endif

}
__INT32_TYPE__ kind_opaque(__INT32_TYPE__ value) __asm__("_kind::opaque");
__INT32_TYPE__ kind_opaque(__INT32_TYPE__ value) {
#if SMITH_TARGET_ARCH == 1
 {
__asm__("" : "+w"(value));
}
#else
 {
__asm__("" : "+nonsense"(value));
}
#endif

return value;
}
__INT32_TYPE__ kind_nested(__INT32_TYPE__ x) __asm__("_kind::nested");
__INT32_TYPE__ kind_nested(__INT32_TYPE__ x) {
constexpr __INT32_TYPE__ local = 3;
__INT32_TYPE__ result = 0;
#if (8) == 8
 {
#if SMITH_TARGET_OS == 1
 {
result = 10;
}
#else
 {
undeclared_function();
}
#endif
#if ((8) == 8 && SMITH_TARGET_FLOAT_RADIX == 2)
 {
result += 1;
}
#endif
#if (8) < 4
 {
undeclared_function();
}
#else
 {
result += x;
}
#endif
#if (3) == 3 && ! ( 1 > 2 ) || 0
 {
result += 100;
}
#endif

}
#endif
#if 0 == 1
 {
undeclared_function();
}
#endif

return result;
} [ [ clang :: unavailable ( "kind::describe is a generic function: call it from a .smith module" ) ] ] void kind_describe ( void ) ;
