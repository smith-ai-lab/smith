[[std::affine]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
void split(Key key, Key keys[3]);
unsigned long long take(Key *key);

unsigned long long fresh(Key key, Key other) {
    unsigned long long a = draw(key);
    key = other;
    return a + draw(key);
}
