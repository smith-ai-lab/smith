__UINT64_TYPE__ smith_text_fill(__UINT64_TYPE__ capacity) __asm__("_smith::text::fill");
__UINT64_TYPE__ smith_text_fill(__UINT64_TYPE__ capacity) {
smith_arena_Arena a = smith_arena_make(capacity);
smith_arena_take(&a, 10);
const __UINT8_TYPE__ bytes[3] = {1, 2, 3};
return smith_arena_take(&a, 5) + smith_arena_checksum(bytes, 3) + smith_arena_limit;
}
