typedef struct {struct smith_array_f32_n_2 {
float * data; __SIZE_TYPE__ count [1]; } rows;
} main_Grid; [ [ clang :: unavailable ( "main::one is a generic function: call it from a .smith module" ) ] ] void main_one ( void ) ;
__INT32_TYPE__ main(void) {
float r[3][2] = {};struct smith_array_f32_n_2 rows = r;
main_Grid g = {.rows = rows};
return (__INT32_TYPE__)__extension__({extern float main_one__Grid(main_Grid*x)__asm__("_main::one(main_Grid)");main_one__Grid(&g);});
}
typedef struct smith_field{struct smith_array_char_n{char*data;__SIZE_TYPE__ count[1];}name;unsigned __int128 type;struct smith_array_char_n doc;}Field;[[maybe_unused]]static inline Field smith_field_make(char*name,__SIZE_TYPE__ length,unsigned __int128 kind,char*doc,__SIZE_TYPE__ doc_length){return(Field){{name,{length}},kind,{doc,{doc_length}}};}extern float main_one__Grid(main_Grid*x)__asm__("_main::one(main_Grid)");
[[gnu::weak]][[gnu::visibility("hidden")]]float main_one__Grid(main_Grid*x){constexpr Field f=smith_call_failed;return x->smith_call_failed[1][0:1][0];}
