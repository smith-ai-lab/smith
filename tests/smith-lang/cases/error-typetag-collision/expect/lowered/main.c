struct main_Stat {
__INT32_TYPE__ size;
};
typedef struct main_Stat main_Stat;
static __INT32_TYPE__ main_Stat(__INT32_TYPE__ fd) {
return fd;
}
__INT32_TYPE__ main(void) {
return main_Stat(0);
}
