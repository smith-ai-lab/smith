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

unsigned long long bad(Key key, unsigned long j) {
    Key keys[3];
    split(key, keys);
    return draw(keys[j]);
}
unsigned long long shifted(Key key) {
    Key keys[3];
    split(key, keys);
    unsigned long long total = 0;
    for (unsigned long i = 0; i < 2; i++)
        total += draw(keys[i + 1]);
    return total;
}
