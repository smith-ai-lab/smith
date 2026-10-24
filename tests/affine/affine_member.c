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

unsigned long long member(Holder h) {
    unsigned long long a = draw(h.key);
    return a + draw(h.key) + h.n;
}
unsigned long long other(Holder h) {
    return draw(h.key) + h.n;
}
