[[std::inline, maybe_unused]] static inline void hint_first(bool condition) {
(void)condition;
}
[[maybe_unused]] static inline __INT32_TYPE__ hint_second(__INT32_TYPE__ x) {
return x + 1;
}
[[nodiscard]] static inline __INT32_TYPE__ hint_third(__INT32_TYPE__ x) {
return x + 2;
}
__INT32_TYPE__ hint_used(__INT32_TYPE__ x) __asm__("_hint::used");
__INT32_TYPE__ hint_used(__INT32_TYPE__ x) {
return hint_third(x);
}
