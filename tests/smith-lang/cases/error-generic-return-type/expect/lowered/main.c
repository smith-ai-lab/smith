[ [ clang :: unavailable ( "main::three is a function over `type`: it exists only at compile time" ) ] ] void main_three ( void ) ; [ [ clang :: unavailable ( "main::divided is a function over `type`: it exists only at compile time" ) ] ] void main_divided ( void ) ; [ [ clang :: unavailable ( "main::not_a_type is a generic function: call it from a .smith module" ) ] ] void main_not_a_type ( void ) ; [ [ clang :: unavailable ( "main::not_computable is a generic function: call it from a .smith module" ) ] ] void main_not_computable ( void ) ; [ [ clang :: unavailable ( "main::not_declared is a generic function: call it from a .smith module" ) ] ] void main_not_declared ( void ) ;
int main(void) {smith_call_failed;smith_call_failed;__extension__({extern int main_not_declared__u8(void)__asm__("_main::not_declared(__UINT8_TYPE__)");main_not_declared__u8();});
return 0;
}
extern int main_not_declared__u8(void)__asm__("_main::not_declared(__UINT8_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]int main_not_declared__u8(void){return 0;}
