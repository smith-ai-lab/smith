#include <stdio.h>
#include "lists.h"

struct main_Node {
__INT32_TYPE__ value;
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") struct main_Node * _Nullable next;
};
typedef struct main_Node main_Node;
typedef _Pragma("clang diagnostic ignored \"-Wnullability-extension\"") main_Node * _Nullable main_MaybeNode;
static main_MaybeNode main_find(main_Node *head, __INT32_TYPE__ wanted) {
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") main_Node * _Nullable at = head;
while (at == .some) {
if (at.some->value == wanted) return at;
at = at.some->next;
}
return .none;
}
static __INT32_TYPE__ main_sum(_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") main_Node * _Nullable list) {
__INT32_TYPE__ total = 0;
for (_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") main_Node * _Nullable cursor = list; cursor != .none; cursor = cursor.some->next) {
total += cursor.some->value;
}
return total;
}
static_assert(sizeof(_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") main_Node * _Nullable) == sizeof(main_Node *));
static_assert(sizeof(main_MaybeNode) == sizeof(void *));
__INT32_TYPE__ main(void) {
main_Node c = {.value = 3};
main_Node b = {.value = 2, .next = &c};
main_Node a = {.value = 1, .next = &b};
main_MaybeNode missing = .none;
lists_Item tail = {.value = 7};
lists_Item head = {.value = 5, .next = &tail};
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") lists_Item * _Nullable end = lists_last(&head);
main_MaybeNode second = main_find(&a, 2);
printf("%d %d %d\n", main_sum(&a), second == .some, missing == .some);
printf("%d\n", main_find(&a, 9) == .none);
if (end == .some) printf("%d\n", end.some->value);
if (second == .some) printf("%d\n", second.some->value);
return 0;
}
