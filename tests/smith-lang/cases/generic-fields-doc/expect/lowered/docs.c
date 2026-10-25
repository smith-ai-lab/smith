#include <stdio.h>

typedef struct {
__UINT16_TYPE__ port;
__UINT16_TYPE__ backlog;
__UINT8_TYPE__ flag;
__INT32_TYPE__ level;
__INT32_TYPE__ quote;
} docs_Options;
typedef struct {
__INT32_TYPE__ a;
} docs_Pair;
typedef struct {
__INT32_TYPE__ undocumented;
} docs_Plain; [ [ clang :: unavailable ( "docs::show is a generic function: call it from a .smith module" ) ] ] void docs_show ( void ) ; [ [ clang :: unavailable ( "docs::value is a generic function: call it from a .smith module" ) ] ] void docs_value ( void ) ;
typedef enum {
docs_Low,
docs_High,
docs_Max,
} docs_Volume;
typedef struct smith_enum_docs_Shape {enum docs_Shape_tag {docs_Shape_tag_round, docs_Shape_tag_none, docs_Shape_tag_plain } tag; union {
float round; };
} __attribute__((annotate("smith::enum"))) docs_Shape;
void docs_run() __asm__("_docs::run");
void docs_run() {__extension__({extern void docs_show__Volume(void)__asm__("_docs::show(docs_Volume)");docs_show__Volume();});__extension__({extern void docs_show__Shape(void)__asm__("_docs::show(docs_Shape)");docs_show__Shape();});__extension__({extern void docs_show__Options(void)__asm__("_docs::show(docs_Options)");docs_show__Options();});__extension__({extern void docs_show__Pair(void)__asm__("_docs::show(docs_Pair)");docs_show__Pair();});__extension__({extern void docs_show__Plain(void)__asm__("_docs::show(docs_Plain)");docs_show__Plain();});__extension__({extern void docs_value__Options(void)__asm__("_docs::value(docs_Options)");docs_value__Options();});
}
extern void docs_show__Volume(void)__asm__("_docs::show(docs_Volume)");
[[gnu::weak]][[gnu::visibility("hidden")]]void docs_show__Volume(void){printf("[%s]\n",smith_call_failed);}
extern void docs_show__Shape(void)__asm__("_docs::show(docs_Shape)");
[[gnu::weak]][[gnu::visibility("hidden")]]void docs_show__Shape(void){printf("[%s]\n",smith_call_failed);}
extern void docs_show__Options(void)__asm__("_docs::show(docs_Options)");
[[gnu::weak]][[gnu::visibility("hidden")]]void docs_show__Options(void){printf("[%s]\n",smith_call_failed);}
extern void docs_show__Pair(void)__asm__("_docs::show(docs_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]]void docs_show__Pair(void){printf("[%s]\n",smith_call_failed);}
extern void docs_show__Plain(void)__asm__("_docs::show(docs_Plain)");
[[gnu::weak]][[gnu::visibility("hidden")]]void docs_show__Plain(void){printf("[%s]\n",smith_call_failed);}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;struct smith_array_char_n doc;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind,char*doc,__SIZE_TYPE__ doc_length){return(Field){{name,{length}},kind,{doc,{doc_length}}};}extern void docs_value__Options(void)__asm__("_docs::value(docs_Options)");
[[gnu::weak]][[gnu::visibility("hidden")]]void docs_value__Options(void){}
