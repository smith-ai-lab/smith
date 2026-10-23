#include <stdio.h>
static int fib(int n) {
return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
static int sum_to(int n) {
int s = 0;
for (int i = 1; i <= n; i++)
s += i;
return s;
}
static unsigned long width(void) {
return sizeof(long);
}
struct Table {
int v[8];
int n;
};
static struct Table squares(int n) {
struct Table t = {};
for (int i = 0; i < n; i++) {
t.v[i] = i * i;
t.n += 1;
}
return t;
}
static_assert(sum_to(100) == 5050);
constexpr int x = fib(10);
constexpr struct Table table = squares(6);
constexpr unsigned long w = width();
enum { Fib7 = fib(7) };
struct Bits {
unsigned a : fib(5);
};
static int cached = fib(12);
int main(void) {
switch (x) {
case fib(10):
printf("%d %d %d %lu %d %d\n", x, table.v[5], table.n, w, Fib7, cached);
}
return 0;
}
