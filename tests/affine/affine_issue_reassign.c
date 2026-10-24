[[smith::affine]] typedef struct { unsigned long long bits; } Key;
typedef struct { Key key; unsigned long long n; } Holder;
unsigned long long draw(Key key);
void split(Key key, Key keys[3]);
unsigned long long take(Key *key);

unsigned long long stream(Key key, unsigned long count) {
    unsigned long long total = 0;
    for (unsigned long i = 0; i < count; i++) {
        Key next[3];
        split(key, next);
        key = next[0];
        total += draw(next[1]);
    }
    return total;
}
