// Recursion in C, or in a .smith file with no `module` line, is C's own: nothing is said (the strictness ladder).
unsigned long fib(unsigned long n) {
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int is_even(unsigned n);

int is_odd(unsigned n) {
    return n != 0 && is_even(n - 1);
}

int is_even(unsigned n) {
    return n == 0 || is_odd(n - 1);
}
