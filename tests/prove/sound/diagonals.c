// proves: get_valid_diagonals
#include "../diagonals.c"
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("get_valid_diagonals", sound_count, {
        Moves moves;
        Board board;
        sound_fill(&moves, sizeof moves);
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                board.cells[row][col] = (Cell)sound_below(4);
        board.turn = (Turn)sound_below(sound_next() % 16 == 0 ? 3 : 2);
        moves.diagonals.len = (uint8_t)sound_between(0, 104);
        moves.jumps.len = sound_u8();
        const uint8_t row = (uint8_t)sound_between(0, 9), col = (uint8_t)sound_between(0, 9);
        SOUND_NOTE("get_valid_diagonals(len %u, turn %d, row %u, col %u)", moves.diagonals.len, board.turn, row, col);
        get_valid_diagonals(&moves, &board, row, col);
    });
})
