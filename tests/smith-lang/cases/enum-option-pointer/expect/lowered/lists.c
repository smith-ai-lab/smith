struct lists_Item {
__INT32_TYPE__ value;
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") struct lists_Item * _Nullable next;
};
typedef struct lists_Item lists_Item;
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") lists_Item * _Nullable lists_last(_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") lists_Item * _Nullable first) __asm__("_lists::last");
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") lists_Item * _Nullable lists_last(_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") lists_Item * _Nullable first) {
_Pragma("clang diagnostic ignored \"-Wnullability-extension\"") lists_Item * _Nullable at = first;
while (at == .some) {
if (at.some->next == .none) return at;
at = at.some->next;
}
return .none;
}
