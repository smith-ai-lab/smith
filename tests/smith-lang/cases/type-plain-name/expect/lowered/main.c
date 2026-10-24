#include <stdio.h>
typedef struct {
int type;
int size;
} Item;
constexpr type wide = __INT64_TYPE__;
static int scaled(int type, int n) {
return (type * n + 1) * 2;
}
static int total(const Item *item, int n) {
int type = item->type;
int sum = type * n;
sum += item->type * item->size;
return sum;
}
int main() {
Item item = {3, 4};
wide big = 5;
printf("%d %d %lld\n", scaled(2, 5), total(&item, 2), (long long)big);
return 0;
}
