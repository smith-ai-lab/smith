#include <stdint.h>
typedef uint64_t (*HookFn)(uint64_t bits);
typedef uint64_t (*hook_table_Plain)(uint64_t bits);
typedef __UINT32_TYPE__ (*hook_table_Sized)(__UINT32_TYPE__ bits);
typedef unsigned long (*hook_table_Builtin)(uint64_t bits);
typedef uint64_t (*hook_table_Private)(uint64_t bits);
typedef struct {
__UINT32_TYPE__ v;
} hook_table_Box;
typedef hook_table_Box (*hook_table_Make)(__UINT32_TYPE__ v);
static uint64_t hook_table_twice(uint64_t bits) {
return bits * 2;
}
static uint64_t (*hook_table_chosen)(uint64_t bits) = hook_table_twice;
uint64_t (*hook_table_hook)(uint64_t bits) = hook_table_twice;
hook_table_Plain hook_table_plain(void) __asm__("_hook::table::plain");
hook_table_Plain hook_table_plain(void) {
hook_table_Private private_twice = hook_table_twice;
return private_twice;
}
uint64_t hook_table_call(HookFn fn, uint64_t bits) __asm__("_hook::table::call");
uint64_t hook_table_call(HookFn fn, uint64_t bits) {
return fn(bits);
}
uint64_t hook_table_chosen_value(uint64_t bits) __asm__("_hook::table::chosen_value");
uint64_t hook_table_chosen_value(uint64_t bits) {
return hook_table_chosen(bits);
}
