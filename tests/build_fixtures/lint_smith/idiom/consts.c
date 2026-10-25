static const int limit = 4;
static const unsigned char table[] = {1, 2, 3};
static const double half = 0.5;
static const char *const names[] = {"a", "b"};
const int shared = 7;
static constexpr int fine = 5;

int f(int n) {
    const int local = 2;
    return n + limit + table[0] + (int)half + (names[0] != nullptr) + shared + fine + local;
}
