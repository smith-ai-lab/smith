typedef struct {
float radius;
} main_Circle;
typedef struct smith_enum_main_Shape {enum main_Shape_tag {main_Shape_tag_circle, main_Shape_tag_empty } tag; union {
main_Circle circle; };
} __attribute__((annotate("smith::enum"))) main_Shape; [ [ clang :: unavailable ( "main::is_empty is a generic function: call it from a .smith module" ) ] ] void main_is_empty ( void ) ; [ [ clang :: unavailable ( "main::pick is a generic function: call it from a .smith module" ) ] ] void main_pick ( void ) ;
