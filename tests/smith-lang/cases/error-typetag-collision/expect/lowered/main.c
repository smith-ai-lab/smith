struct main_stat {
__INT32_TYPE__ size;
};
typedef struct main_stat main_stat;
static __INT32_TYPE__ main_stat(__INT32_TYPE__ fd) {
return fd;
}
__INT32_TYPE__ main(void) {
return main_stat(0);
}
