// smith_modifies (issue #54): what a function writes, so that its callers keep everything else.
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
  int32_t a[4];
  int32_t b[4];
  int32_t c;
  int32_t len_a;
  int32_t len_b;
} Pair;

// Appends to a, and writes nothing else: it names the list and its length, and says nothing of b, c or len_b.
void push_a(Pair *pair, int32_t value) {
  smith_requires(0 <= pair->len_a && pair->len_a < 4);
  smith_modifies(&pair->a, sizeof pair->a);
  smith_modifies(&pair->len_a, sizeof pair->len_a);
  const int32_t old = pair->len_a;
  pair->a[pair->len_a] = value;
  pair->len_a += 1;
  smith_ensures(pair->len_a == old + 1 && pair->a[old] == value);
}

// A caller relies on what push_a never named being unchanged, though push_a promises nothing of it.
void keeps_the_rest(Pair *pair) {
  smith_requires(0 <= pair->len_a && pair->len_a < 4);
  const int32_t c = pair->c;
  const int32_t len_b = pair->len_b;
  const int32_t b0 = pair->b[0];
  push_a(pair, 7);
  smith_assert(pair->c == c && pair->len_b == len_b && pair->b[0] == b0);
}

// ... and what it did name is not known.
void relies_on_a_named_field(Pair *pair) {
  smith_requires(0 <= pair->len_a && pair->len_a < 4);
  const int32_t first = pair->a[0];
  push_a(pair, 7);
  smith_assert(pair->a[0] == first);
}

// The write outside the ranges is refuted at its line.
void writes_outside(Pair *pair) {
  smith_requires(true);
  smith_modifies(&pair->a, sizeof pair->a);
  pair->b[1] = 3;
  smith_ensures(true);
}

// A callee that names a range is a write of it: the caller must name it too.
void names_too_little(Pair *pair) {
  smith_requires(0 <= pair->len_a && pair->len_a < 4);
  smith_modifies(&pair->a, sizeof pair->a);
  push_a(pair, 1);
  smith_ensures(true);
}

// With no range at all the function writes nothing outside its locals.
int32_t total(const Pair *pair) {
  smith_modifies(nullptr, 0);
  int32_t local[2] = {pair->c, pair->len_a};
  local[0] = local[1];
  smith_ensures(true);
  return local[0];
}

void writes_though_empty(Pair *pair) {
  smith_modifies(nullptr, 0);
  pair->c = 1;
  smith_ensures(true);
}

void nothing(Pair *pair) {
  smith_modifies(nullptr, 0);
  smith_ensures(true);
}

void after_nothing(Pair *pair) {
  const Pair before = *pair;
  nothing(pair);
  smith_assert(pair->c == before.c && pair->a[2] == before.a[2] && pair->len_b == before.len_b);
}

// A pointer and a size: a range of an array that is not a field.
void fill(int32_t xs[static 4], int32_t n) {
  smith_requires(0 < n && n <= 4);
  smith_modifies(xs, n * sizeof(int32_t));
  for (int32_t i = 0; i < n; i++) xs[i] = 0;
  smith_ensures(true);
}

void fill_then(Pair *pair) {
  const int32_t c = pair->c;
  const int32_t b2 = pair->b[2];
  fill(pair->b, 2);
  smith_assert(pair->c == c && pair->b[2] == b2);
}

void fill_too_far(int32_t xs[static 4]) {
  smith_modifies(xs, 2 * sizeof(int32_t));
  xs[2] = 1;
  smith_ensures(true);
}
