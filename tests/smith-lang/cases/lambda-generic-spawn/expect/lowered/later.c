typedef void (*later_Entry)(void *);
typedef struct {
later_Entry entry;
alignas(16) __UINT8_TYPE__ data[64];
} later_Slot; [ [ clang :: unavailable ( "later::defer is a generic function: call it from a .smith module" ) ] ] void later_defer ( void ) ;
