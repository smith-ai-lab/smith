// The barriers of std::optimize (include/smith/optimize_asm.h): an empty asm that takes the value in and gives it back,
// so the optimizer treats the result as unknown and the argument as used. This is the one place in smith's libraries
// where extended asm is written; a .smith file cannot hold it, so std::optimize calls these.
#include <smith/optimize_asm.h>

double smith_opaque_f64(double x) {
#if defined(__aarch64__)
    __asm__("" : "+w"(x));
#else
    __asm__("" : "+X"(x));
#endif
    return x;
}

float smith_opaque_f32(float x) {
#if defined(__aarch64__)
    __asm__("" : "+w"(x));
#else
    __asm__("" : "+X"(x));
#endif
    return x;
}

uint32_t smith_opaque_u32(uint32_t x) {
    __asm__("" : "+r"(x));
    return x;
}

uint64_t smith_opaque_u64(uint64_t x) {
    __asm__("" : "+r"(x));
    return x;
}

const void *smith_opaque_ptr(const void *x) {
    __asm__("" : "+r"(x));
    return x;
}
