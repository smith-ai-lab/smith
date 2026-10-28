[[std::linear]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
unsigned long long take(Key *key);

unsigned long long address(Key key) {
    Key *p = &key;
    return draw(key) + take(p);
}
