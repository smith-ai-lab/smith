[ [ clang :: unavailable ( "main::Bad is a function over `Type`: it exists only at compile time" ) ] ] void main_Bad ( void ) ; [ [ clang :: unavailable ( "main::Worse is a function over `Type`: it exists only at compile time" ) ] ] void main_Worse ( void ) ;
int main(void) {
constexpr Type Q = main_Bad(float);
constexpr Type R = main_Worse(float);
return 0;
}
