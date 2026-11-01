[ [ clang :: unavailable ( "stats::twice is a generic function: call it from a .smith module" ) ] ] void stats_twice ( void ) ; [ [ clang :: unavailable ( "stats::larger is a generic function: call it from a .smith module" ) ] ] void stats_larger ( void ) ; [ [ clang :: unavailable ( "stats::width is a generic function: call it from a .smith module" ) ] ] void stats_width ( void ) ; [ [ clang :: unavailable ( "stats::sign_of is a generic function: call it from a .smith module" ) ] ] void stats_sign_of ( void ) ; [ [ clang :: unavailable ( "stats::shifted is a generic function: call it from a .smith module" ) ] ] void stats_shifted ( void ) ;
__INT32_TYPE__ stats_plain(__INT32_TYPE__ q) __asm__("_stats::plain");
__INT32_TYPE__ stats_plain(__INT32_TYPE__ q) {
Int r = q * 2;
return r;
}
