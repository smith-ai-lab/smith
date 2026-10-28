[ [ clang :: unavailable ( "order::ascending is a generic function: call it from a .smith module" ) ] ] void order_ascending ( void ) ; [ [ clang :: unavailable ( "order::smallest is a generic function: call it from a .smith module" ) ] ] void order_smallest ( void ) ; [ [ clang :: unavailable ( "order::smallest_of is a generic function: call it from a .smith module" ) ] ] void order_smallest_of ( void ) ; [ [ clang :: unavailable ( "order::take is a generic function: call it from a .smith module" ) ] ] void order_take ( void ) ;
void order_take_use() __asm__("_order::take_use");
void order_take_use() {smith_call_failed;
} [ [ clang :: unavailable ( "order::address_of is a generic function: call it from a .smith module" ) ] ] void order_address_of ( void ) ;
__SIZE_TYPE__ order_address_use() __asm__("_order::address_use");
__SIZE_TYPE__ order_address_use() {
return smith_call_failed;
}
