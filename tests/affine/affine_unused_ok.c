[[smith::affine]] typedef struct {
    unsigned long long bits;
} Key;
typedef struct {
    Key key;
    unsigned long long n;
} Holder;
unsigned long long draw(Key key);
void split(Key key, Key keys[3]);
unsigned long long take(Key *key);

unsigned long long unused(Key key) {
    (void)key;
    return 0;
}
unsigned long long once(Key key) {
    return draw(key);
}
