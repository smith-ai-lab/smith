static __INT32_TYPE__ main_first[];
static __INT32_TYPE__ main_grid[3][] = {{1}, {2}, {3}};
__INT32_TYPE__ main(void) {
return main_first[0] + main_grid[0][0];
}
