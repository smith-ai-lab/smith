static __INT32_TYPE__ main_xs[] = {1, 2, 3};
extern const __INT32_TYPE__ table[];
typedef struct {
__INT32_TYPE__ count;
__INT32_TYPE__ items[];
} main_Bag;
__INT32_TYPE__ main(void) {
__INT32_TYPE__ *p = (__INT32_TYPE__[]){1, 2};
return main_xs[0] + p[0];
}
