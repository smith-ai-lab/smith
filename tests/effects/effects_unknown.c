// Calls the file cannot see into: a function pointer (`unknown`, every effect), a declaration of the project with no body and
// no attribute (another file may define it: nothing is said here, the link of the program says, see
// tests/effects_infer_test.smith), and one with an attribute (a promise smith trusts).
int run_unknown(int);
[[smith::effects(alloc)]] void *promised(unsigned);

[[smith::pure]] int indirect(int (*callback)(void)) {
    return callback();
}

[[smith::pure]] int external(int value) {
    return run_unknown(value);
}

[[smith::pure]] void *promise_breaks_pure(unsigned size) {
    return promised(size);
}

[[smith::effects(alloc)]] void *promise_fits(unsigned size) {
    return promised(size);
}
