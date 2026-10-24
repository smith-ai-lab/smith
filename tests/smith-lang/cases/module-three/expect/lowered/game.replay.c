#include "lib/text.h"

typedef struct { __UINT64_TYPE__ frames; lib_text_Text name; } game_replay_Replay;
constexpr __UINT32_TYPE__ game_replay_version = 3;
extern __UINT32_TYPE__ game_replay_counter __asm__("_game::replay::counter");
__UINT32_TYPE__ game_replay_counter __asm__("_game::replay::counter") = 7;
typedef struct game_replay_Handle { __INT32_TYPE__ fd; } game_replay_Handle;
enum game_replay_Mode { game_replay_Mode_Fast, game_replay_Mode_Slow };
static __UINT32_TYPE__ game_replay_helper() {
return game_replay_version + game_replay_counter;
}
game_replay_Replay game_replay_open(game_replay_Handle *handle, const char *path) __asm__("_game::replay::open");
game_replay_Replay game_replay_open(game_replay_Handle *handle, const char *path) {
game_replay_Replay replay = {0};
replay.frames = game_replay_helper() + (__UINT64_TYPE__)handle->fd;
replay.name = lib_text_make(path);
game_replay_counter += 1;
return replay;
}
__UINT64_TYPE__ game_replay_checksum(const __UINT8_TYPE__ *data, __SIZE_TYPE__ size) __asm__("_game::replay::checksum");
