[[std::linear]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
unsigned long long take(Key *key);

unsigned long long overwrite(Key key, Key other) {
    key = other;
    return draw(key);
}
unsigned long long after_use(Key key, Key other) {
    unsigned long long a = draw(key);
    key = other;
    return a + draw(key);
}
