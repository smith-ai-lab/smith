[[smith::affine]] typedef struct {
    unsigned long long bits;
    const int *tap;
} Key;
unsigned long long draw(Key key);

unsigned long long write_then_use(const int *tap) {
    Key key = {1, nullptr};
    key.tap = tap;
    return draw(key);
}
