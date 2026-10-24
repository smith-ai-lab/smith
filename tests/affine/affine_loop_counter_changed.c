[[smith::affine]] typedef struct { unsigned long long bits; } Key;
typedef struct { Key key; unsigned long long n; } Holder;
unsigned long long draw(Key key);
void split(Key key, Key keys[3]);
unsigned long long take(Key *key);

unsigned long long skipping(Key key) {
    Key keys[3];
    split(key, keys);
    unsigned long long total = 0;
    for (unsigned long i = 0; i < 3; i++) {
        total += draw(keys[i]);
        i++;
    }
    return total;
}
