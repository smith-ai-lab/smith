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

unsigned long long elements(Key key) {
    Key keys[3];
    split(key, keys);
    unsigned long long a = draw(keys[0]);
    unsigned long long b = draw(keys[1]);
    unsigned long long c = draw(keys[0]);
    return a + b + c;
}
