// Calls the file cannot see into: a function pointer, a declaration with no body and no attribute (`c`, every effect),
// and one with an attribute (a promise smith trusts).
int run_unknown(int);
[[smith::alloc]] void *promised(unsigned);

[[smith::pure]] int indirect(int (*callback)(void)) {
    return callback();
}

[[smith::pure]] int external(int value) {
    return run_unknown(value);
}

[[smith::pure]] void *promise_breaks_pure(unsigned size) {
    return promised(size);
}

[[smith::alloc]] void *promise_fits(unsigned size) {
    return promised(size);
}
