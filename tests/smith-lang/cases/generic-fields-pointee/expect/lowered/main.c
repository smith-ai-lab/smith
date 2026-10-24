#include <stdio.h>

typedef struct {
__INT32_TYPE__ id;
double weight;
} main_Inner;
typedef struct {
main_Inner in;
main_Inner *link;
__INT32_TYPE__ count;
} main_Outer; [ [ clang :: unavailable ( "main::show_pointee is a generic function: call it from a .smith module" ) ] ] void main_show_pointee ( void ) ; [ [ clang :: unavailable ( "main::show_member is a generic function: call it from a .smith module" ) ] ] void main_show_member ( void ) ;
__INT32_TYPE__ main() {
main_Outer o = {};smith_call_failed;smith_call_failed;smith_call_failed;
return 0;
}
