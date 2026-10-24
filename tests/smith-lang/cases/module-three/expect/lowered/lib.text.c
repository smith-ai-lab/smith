#include <string.h>

typedef struct { const char *data; __SIZE_TYPE__ length; } lib_text_Text;
lib_text_Text lib_text_make(const char *data) __asm__("_lib::text::make");
lib_text_Text lib_text_make(const char *data) {
return (lib_text_Text){data, strlen(data)};
}
__SIZE_TYPE__ lib_text_length(lib_text_Text text) __asm__("_lib::text::length");
__SIZE_TYPE__ lib_text_length(lib_text_Text text) {
return text.length;
}
static __SIZE_TYPE__ lib_text_unused_helper() {
return 0;
}
