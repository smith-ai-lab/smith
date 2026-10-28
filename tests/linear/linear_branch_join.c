[[std::linear]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
unsigned long long take(Key *key);

unsigned long long join(Key key, int c) {
    unsigned long long a = 0;
    if (c) a = draw(key);
    return a + draw(key);
}
unsigned long long branches(Key key, int c) {
    if (c) return draw(key);
    return draw(key) + 1;
}
