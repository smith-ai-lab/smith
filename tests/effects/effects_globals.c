// Writing a global or a function-local static is `globals`; reading a constant is not.
static int counter;
static const int limit = 4;

[[smith::pure]] int reads(void) {
    return limit;
}

[[smith::pure]] int bump(void) {
    counter++;
    return counter;
}

[[smith::pure]] int remember(int value) {
    static int last;
    last = value;
    return last;
}
