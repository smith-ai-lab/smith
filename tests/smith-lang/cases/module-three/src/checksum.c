#include <stddef.h>
#include <stdint.h>

#include "game/replay.h"

uint64_t game_replay_checksum(const uint8_t *data, size_t size) {
    uint64_t sum = 0;
    for (size_t i = 0; i < size; i++) sum = sum * 31 + data[i];
    return sum;
}
