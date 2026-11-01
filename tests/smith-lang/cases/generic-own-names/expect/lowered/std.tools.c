#include <stdio.h>

extern __INT32_TYPE__ smith_tools_offset __asm__("_std::tools::offset");
__INT32_TYPE__ smith_tools_offset __asm__("_std::tools::offset") = 10;
__INT32_TYPE__ smith_tools_pick() __asm__("_std::tools::pick");
__INT32_TYPE__ smith_tools_pick() {
return smith_tools_offset + 1;
} [ [ clang :: unavailable ( "std::tools::shifted is a generic function: call it from a .smith module" ) ] ] void smith_tools_shifted ( void ) ; [ [ clang :: unavailable ( "std::tools::table_sum is a generic function: call it from a .smith module" ) ] ] void smith_tools_table_sum ( void ) ; [ [ clang :: unavailable ( "std::tools::nested is a generic function: call it from a .smith module" ) ] ] void smith_tools_nested ( void ) ;
