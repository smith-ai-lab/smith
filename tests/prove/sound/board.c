// proves: cell occupied row_count board_count
#include "../board.c"
#define Board ScaleBoard
#include "../scale.c"
#undef Board
#include "sound.h"

static void board_fill(Board *board) {
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            board->cells[row][col] = (sound_next() & 3) == 0 ? (uint8_t)sound_u8() : 0;
}

SOUND_MAIN({
    SOUND_RUN("cell", sound_count, {
        Board board;
        board_fill(&board);
        const uint8_t row = (uint8_t)sound_between(0, 9), col = (uint8_t)sound_between(0, 9);
        SOUND_NOTE("cell(board, %u, %u)", row, col);
        (void)cell(&board, row, col);
    });
    SOUND_RUN("occupied", sound_count, {
        Board board;
        board_fill(&board);
        SOUND_NOTE("occupied(board)");
        (void)occupied(&board);
    });
    SOUND_RUN("row_count", sound_count, {
        ScaleBoard board;
        sound_fill(&board, sizeof board);
        const uint8_t row = (uint8_t)sound_between(0, 9);
        SOUND_NOTE("row_count(board, %u)", row);
        (void)row_count(&board, row);
    });
    SOUND_RUN("board_count", sound_count, {
        ScaleBoard board;
        sound_fill(&board, sizeof board);
        SOUND_NOTE("board_count(board)");
        (void)board_count(&board);
    });
})
