// proves: apply_move count_of bump_count is_step
#include "../by_value.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("apply_move", sound_count, {
        Board board;
        sound_fill(&board, sizeof board);
        const Move move = {(uint8_t)sound_between(0, 9), (uint8_t)sound_between(0, 9), (uint8_t)sound_between(0, 9),
                           (uint8_t)sound_between(0, 9)};
        SOUND_NOTE("apply_move(board, {%u,%u,%u,%u})", move.row, move.col, move.dest_row, move.dest_col);
        (void)apply_move(&board, move);
    });
    SOUND_RUN("count_of", sound_count, {
        Padded padded;
        sound_fill(&padded, sizeof padded);
        SOUND_NOTE("count_of({%u, %u})", padded.flag, padded.count);
        (void)count_of(padded);
    });
    SOUND_RUN("bump_count", sound_count, {
        Padded padded;
        sound_fill(&padded, sizeof padded);
        if (sound_next() & 1) padded.count = sound_u32() % 110 + (sound_next() & 1 ? 0 : UINT32_MAX - 5);
        SOUND_NOTE("bump_count({%u, %u})", padded.flag, padded.count);
        (void)bump_count(padded);
    });
    SOUND_RUN("is_step", sound_count, {
        Move move;
        sound_fill(&move, sizeof move);
        if (sound_next() & 1) move.dest_row = (uint8_t)(move.row + 1);
        SOUND_NOTE("is_step({%u,%u,%u,%u})", move.row, move.col, move.dest_row, move.dest_col);
        (void)is_step(move);
    });
})
