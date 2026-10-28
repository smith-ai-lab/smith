[ [ clang :: unavailable ( "lists::push is a generic function: call it from a .smith module" ) ] ] void lists_push ( void ) ; [ [ clang :: unavailable ( "lists::total is a generic function: call it from a .smith module" ) ] ] void lists_total ( void ) ;
static __SIZE_TYPE__ lists_storage[4];struct smith_struct_List_usize {__SIZE_TYPE__ *items;
__SIZE_TYPE__ count; };
struct smith_struct_List_usize lists_filled(__SIZE_TYPE__ first) __asm__("_lists::filled");
struct smith_struct_List_usize lists_filled(__SIZE_TYPE__ first) {
struct smith_struct_List_usize list = {.items = lists_storage};smith_call_failed;smith_call_failed;
return list;
}
