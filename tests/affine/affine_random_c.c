#include <smith/random.h>

double two_draws(SmithKey key) {
    SmithKey keys[3];
    smith_key_split_n(key, keys, 3);
    double a = smith_key_unit_double(keys[0]);
    double b = smith_key_unit_double(keys[0]);
    return a + b;
}

void plain_loop(SmithKey key, double xs[3]) {
    SmithKey per_item[3];
    smith_key_split_n(key, per_item, 3);
    for (unsigned long i = 0; i < 3; i++)
        xs[i] = smith_key_unit_double(per_item[i]);
}

double reuse(SmithKey key) {
    double a = smith_key_unit_double(key);
    return a + smith_key_unit_double(key);
}
