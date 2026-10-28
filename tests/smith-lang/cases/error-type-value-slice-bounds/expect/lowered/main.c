[ [ clang :: unavailable ( "main::bad is a function over `type`: it exists only at compile time" ) ] ] void main_bad ( void ) ; [ [ clang :: unavailable ( "main::worse is a function over `type`: it exists only at compile time" ) ] ] void main_worse ( void ) ;
int main(void) {
constexpr type q = main_bad(float);
constexpr type r = main_worse(float);
return 0;
}
