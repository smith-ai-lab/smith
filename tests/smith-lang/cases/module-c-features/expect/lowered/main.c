#include "kit/things.h"
#include <stdio.h>
#define OPEN_SUM(first) k::sum(first)
#define PICK(i) k::pick(i)
static int increment(int value) {
return value + 1;
}
int main() {
kit_things_Node last = {.next = nullptr, .value = 3, .mode = 2, .point = {.x = 1, .y = 2}};
kit_things_Node first = {.next = &last, .value = 4, .mode = 1};
printf("%d %d\n", OPEN_SUM(&first), kit_things_run(increment, 5));
printf("%u %u %zu %s\n", PICK(1), PICK(9), (__SIZE_TYPE__)kit_things_capacity, kit_things_names[2]);
kit_things_Word word = kit_things_make_word(0x01020304);
printf("%u %u\n", word.bytes[0], (unsigned)sizeof(kit_things_Word));
printf("%d %d %d\n", kit_things_by_type(7), kit_things_twice_local(21), kit_things_triple(5));
kit_things_say("said");
return 0;
}
