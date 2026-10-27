// A borrow in plain C: `&key` as the argument of a `Key *` parameter is no use; the one consuming use is checked.
[[smith::affine]] typedef struct {
    unsigned long long bits;
} Key;
unsigned long long draw(Key key);
unsigned long long peek(const Key *key);
void bump(Key *key);

unsigned long long demo(Key key) {
    bump(&key);
    unsigned long long a = peek(&key);
    return a + draw(key) + draw(key);
}
