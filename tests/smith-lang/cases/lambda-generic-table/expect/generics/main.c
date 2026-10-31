__extension__({ extern main_Route main_route__add_one(const char * path) __asm__("_main::route(main_add_one)@src/main.smith"); main_route__add_one("/a"); })
__extension__({ extern main_Route main_route__triple(const char * path) __asm__("_main::route(main_triple)@src/main.smith"); main_route__triple("/b"); })
static i32 main_route__add_one__lambda0(__INT32_TYPE__ x) {
    return main_add_one(x);
}
extern main_Route main_route__add_one(const char * path) __asm__("_main::route(main_add_one)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] main_Route main_route__add_one(const char * path) {
    main_Route r = {
        path, main_route__add_one__lambda0 }
    ;
    return r;
}
static i32 main_route__triple__lambda0(__INT32_TYPE__ x) {
    return main_triple(x);
}
extern main_Route main_route__triple(const char * path) __asm__("_main::route(main_triple)@src/main.smith");
[[gnu::weak]][[gnu::visibility("hidden")]] main_Route main_route__triple(const char * path) {
    main_Route r = {
        path, main_route__triple__lambda0 }
    ;
    return r;
}
