// proves: turn_after add_if_set spread_then_check spread
#include "../readonly.c"
#include "sound.h"

static void list_make(List *list) {
    sound_fill(list, sizeof *list);
    list->len = (uint8_t)sound_between(0, 18);
}

static void board_make(Board *board) {
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            board->cells[row][col] = (int32_t)sound_below(3);
    board->turn = sound_i32();
}

SOUND_MAIN({
    SOUND_RUN("add_if_set", sound_count, {
        List list;
        Board board;
        list_make(&list);
        board_make(&board);
        const uint8_t row = (uint8_t)sound_between(0, 9), col = (uint8_t)sound_between(0, 9);
        SOUND_NOTE("add_if_set(len %u, row %u, col %u)", list.len, row, col);
        add_if_set(&list, &board, row, col);
    });
    SOUND_RUN("turn_after", sound_count, {
        List list;
        Board board;
        list_make(&list);
        board_make(&board);
        const uint8_t row = (uint8_t)sound_between(0, 9), col = (uint8_t)sound_between(0, 9);
        SOUND_NOTE("turn_after(len %u, row %u, col %u)", list.len, row, col);
        (void)turn_after(&list, &board, row, col);
    });
    SOUND_RUN("spread", sound_count, {
        List list;
        list_make(&list);
        const uint8_t x = sound_u8();
        SOUND_NOTE("spread(len %u, 0x%x)", list.len, x);
        spread(&list, x);
    });
    SOUND_RUN("spread_then_check", sound_count, {
        List list;
        list_make(&list);
        const uint8_t x = sound_u8();
        SOUND_NOTE("spread_then_check(len %u, 0x%x)", list.len, x);
        (void)spread_then_check(&list, x);
    });
})
