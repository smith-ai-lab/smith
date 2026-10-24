#include <stdio.h>

extern __INT32_TYPE__ smith_tools_offset __asm__("_smith::tools::offset");
__INT32_TYPE__ smith_tools_offset __asm__("_smith::tools::offset") = 10;
__INT32_TYPE__ smith_tools_pick() __asm__("_smith::tools::pick");
__INT32_TYPE__ smith_tools_pick() {
return smith_tools_offset + 1;
} [ [ clang :: unavailable ( "smith::tools::shifted is a generic function: call it from a .smith module" ) ] ] void smith_tools_shifted ( void ) ; [ [ clang :: unavailable ( "smith::tools::table_sum is a generic function: call it from a .smith module" ) ] ] void smith_tools_table_sum ( void ) ; [ [ clang :: unavailable ( "smith::tools::nested is a generic function: call it from a .smith module" ) ] ] void smith_tools_nested ( void ) ;
