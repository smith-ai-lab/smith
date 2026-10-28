#include <stdio.h>

struct main_Request {
__INT32_TYPE__ id;
};
typedef struct main_Request main_Request;
struct main_Response {
__INT32_TYPE__ code;
};
typedef struct main_Response main_Response;
static constexpr Type main_Handler = main_Response (*)(main_Request *, __INT32_TYPE__);
static constexpr Type main_Callback = void (*)(void);
static constexpr Type main_Block = __UINT8_TYPE__[16];
static constexpr Type main_Grid = float[2][3];
static constexpr Type main_Row = __INT32_TYPE__ (*)[4];
static constexpr Type main_Pointers = main_Request *[4];
static_assert(Pointer(main_Handler) && Pointer(main_Callback) && Pointer(main_Row));
static_assert(Array(main_Block) && Array(main_Grid) && Array(main_Pointers));
static_assert(sizeof(main_Block) == 16 && sizeof(main_Grid) == 24 && sizeof(main_Pointers) == 32 && sizeof(main_Row) == 8);
static_assert(main_Handler != main_Callback && main_Block != main_Grid);
static main_Response main_answer(main_Request *request, __INT32_TYPE__ extra) {
return (main_Response){request->id + extra};
}
static void main_nothing(void) {}
__INT32_TYPE__ main(void) {
main_Handler handler = main_answer;
main_Callback callback = main_nothing;
main_Block block = {1, 2, 3};
main_Grid grid = {{1, 2, 3}, {4, 5, 6}};
__INT32_TYPE__ numbers[4] = {10, 20, 30, 40};
main_Row row = &numbers;
main_Request request = {5};
main_Pointers pointers = {&request};
callback();
printf("%d %d %d %d %d %d\n", handler(&request, 2).code, (__INT32_TYPE__)sizeof(block), block[2], (__INT32_TYPE__)grid[1][2],
(*row)[3], pointers[0]->id);
return 0;
}
