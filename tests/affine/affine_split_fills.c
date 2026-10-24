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

unsigned long long refill(Key key, Key again) {
    Key keys[3];
    split(key, keys);
    unsigned long long a = draw(keys[0]);
    split(again, keys);
    return a + draw(keys[0]);
}
