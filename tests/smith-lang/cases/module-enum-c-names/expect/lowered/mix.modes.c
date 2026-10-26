typedef enum {
MixMode_Fast,
MixMode_Slow,
} MixMode;
enum mix_modes_Level {
mix_modes_Level_Low,
mix_modes_Level_High,
};
typedef enum mix_modes_Level mix_modes_Level;
__INT32_TYPE__ mix_modes_score(MixMode mode, enum mix_modes_Level level) __asm__("_mix::modes::score");
__INT32_TYPE__ mix_modes_score(MixMode mode, enum mix_modes_Level level) {
return (mode == MixMode_Slow ? 10 : 1) + (level == mix_modes_Level_High ? 100 : 0);
}
