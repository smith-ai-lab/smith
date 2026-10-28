[[std::linear]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
unsigned long long take(Key *key);

unsigned long long used(Key key) {
    return draw(key);
}
unsigned long long ends(Key key) {
    Key other = key;
    return 1;
}
unsigned long long early(Key key, int c) {
    if (c) return 0;
    return draw(key);
}
unsigned long long one_branch(Key key, int c) {
    unsigned long long n = 0;
    if (c) n = draw(key);
    return n;
}
unsigned long long both_branches(Key key, int c) {
    if (c) return draw(key);
    return draw(key) + 1;
}
void leave(unsigned long n) {
    for (unsigned long i = 0; i < n; i++) {
        Key inner = {i};
        if (i == 2) break;
        if (i == 3) continue;
        (void)draw(inner);
    }
}
void declared_later(int c) {
    Key made;
    if (c) made = (Key){1ULL};
}
