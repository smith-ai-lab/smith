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
#ifndef SMITH_TARGET_ASSERTIONS
#ifdef NDEBUG
#define SMITH_TARGET_ASSERTIONS 0
#else
#define SMITH_TARGET_ASSERTIONS 1
#endif
#endif
#ifndef SMITH_TARGET_STATIC_ANALYZER
#ifdef __clang_analyzer__
#define SMITH_TARGET_STATIC_ANALYZER 1
#else
#define SMITH_TARGET_STATIC_ANALYZER 0
#endif
#endif
[ [ clang :: unavailable ( "guard::pick_one is a generic function: call it from a .smith module" ) ] ] void guard_pick_one ( void ) ;
