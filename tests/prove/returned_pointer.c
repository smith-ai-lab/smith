// A pointer returned under an smith_ensures is a whole object of its type, aligned, or null: proved where the
// function returns it, so a caller that loads through it needs only smith_ensures(result != nullptr) (#74: game's
// game_mesh_kind). A function that returns a pointer that is not one (one past the end) is refused where it returns it.
#include <smith/contract.h>
#include <stdint.h>

typedef struct {
    uint32_t first;
    uint32_t count;
} Kind;

static const Kind kinds[4] = {{0, 3}, {3, 5}, {8, 1}, {9, 7}};

static const Kind *find(uint32_t id) {
    smith_requires(id < 16);
    const Kind *found = nullptr;
    for (uint32_t i = 0; i < 4; i++) {
        if (id >= kinds[i].first && id < kinds[i].first + kinds[i].count) {
            found = &kinds[i];
            break;
        }
    }
    smith_ensures(found != nullptr);
    return found;
}

uint32_t offset_in_kind(uint32_t id) {
    smith_requires(id < 16);
    const Kind *kind = find(id);
    uint32_t offset = id - kind->first;
    smith_ensures(true);
    return offset;
}

[[gnu::used]] static const Kind *past_the_end(void) {
    smith_ensures(true);
    return &kinds[4];
}
