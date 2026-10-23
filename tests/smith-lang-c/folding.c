// Where C does not require a constant, a call is not evaluated at compile time: this file means the same in a .c file,
// in a .smith file without a module line, and in a smith that has no evaluator for C functions.
static int f(int n) {
    return n * 2 + 1;
}
static int g(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i;
    return s;
}

int is_constant(void) {
    return __builtin_constant_p(f(1)); // 0: quiet folding is not a constant context
}
int variable_length(int k) {
    int a[f(2)]; // a variable-length array, as in C
    int b[g(5)];
    a[0] = k;
    b[0] = k;
    return (int)(sizeof a + sizeof b) + a[0] + b[0];
}
int gnu_folded(void) {
    static const int y = 4;
    int c[y + 1]; // folded by GNU extension, not by the evaluator
    c[0] = 1;
    return (int)sizeof c + c[0];
}
int automatic(void) {
    int x = f(3) + g(4); // an automatic initializer is not evaluated at compile time
    return x;
}
int main(void) {
    return is_constant() + variable_length(1) + gnu_folded() + automatic();
}
