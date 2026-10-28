[[std::linear]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
unsigned long long take(Key *key);

unsigned long long loop(Key key, unsigned long n) {
    unsigned long long total = 0;
    for (unsigned long i = 0; i < n; i++)
        total += draw(key);
    return total;
}
