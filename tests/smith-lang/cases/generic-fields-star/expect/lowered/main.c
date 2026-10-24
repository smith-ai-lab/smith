#include <stdio.h>
#include <stdlib.h>

typedef struct {struct smith_array_char_ptr_n {
char * * data; __SIZE_TYPE__ count [1]; }names;
__INT32_TYPE__ n;
} main_Table;
void smith_failure(__UINT8_TYPE__ code);
void smith_failure(__UINT8_TYPE__ code) {
exit(code);
} [ [ clang :: unavailable ( "main::show is a generic function: call it from a .smith module" ) ] ] void main_show ( void ) ;
__INT32_TYPE__ main(void) {
char *words[2] = {"one", "two"};
main_Table t = {.names = words[0:2], .n = 2};__extension__({extern void main_show__Table(main_Table*x)__asm__("_main::show(main_Table)");main_show__Table(&t);});
return 0;
}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind){return(Field){{name,{length}},kind};}extern void main_show__Table(main_Table*x)__asm__("_main::show(main_Table)");
[[gnu::weak]][[gnu::visibility("hidden")]]void main_show__Table(main_Table*x){}
