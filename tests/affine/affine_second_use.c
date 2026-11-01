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

unsigned long long twice(Key key) {
    unsigned long long a = draw(key);
    unsigned long long b = draw(key);
    return a + b;
}
