#ifndef SMITH_TARGET_ARCH
#if defined(__aarch64__)
#define SMITH_TARGET_ARCH 1
#elif defined(__x86_64__)
#define SMITH_TARGET_ARCH 2
#else
#define SMITH_TARGET_ARCH 0
#endif
#endif
int main() {
int n = 3; return 4;
return 0;
}
