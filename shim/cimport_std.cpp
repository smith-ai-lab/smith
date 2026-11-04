// The edge for front::compile's loader (#2041): the sources of smith's own `std` modules, held by the program that
// embeds them. `smith cc --front-end=new` finds `import std.console` in no folder of a user's project: the module's
// source is inside the smith binary (`smith_libraries` in smith.c), which hands each one to the front end through
// this table before a compile. The front end's own test runner has no embedded libraries, so the table starts empty
// and a test adds a stand-in.
//
//   smith_std_source_add(name, source, size)  remembers the source of `std.<name>` (not copied: it must outlive the
//                                             compile); a second add of a name replaces the first
//   smith_std_source(name, size)              the source of `std.<name>` and its size, or null for an unknown name
#include <cstddef>
#include <cstring>

namespace {

constexpr size_t table_size = 256;

struct Entry {
    const char *name;
    const char *source;
    size_t size;
};

Entry table[table_size];
size_t used = 0;

} // namespace

extern "C" void smith_std_source_add(const char *name, const char *source, size_t size) {
    for (size_t i = 0; i < used; i++) {
        if (std::strcmp(table[i].name, name) != 0) continue;
        table[i].source = source;
        table[i].size = size;
        return;
    }
    if (used == table_size) return;
    table[used++] = Entry{name, source, size};
}

extern "C" const char *smith_std_source(const char *name, size_t *size) {
    for (size_t i = 0; i < used; i++) {
        if (std::strcmp(table[i].name, name) != 0) continue;
        *size = table[i].size;
        return table[i].source;
    }
    *size = 0;
    return nullptr;
}
