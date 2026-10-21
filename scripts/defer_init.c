// Moves a Mach-O object's static initializers out of the section dyld runs at load: `defer_init in.o out.o name`.
//
// clang-format, clang-tidy and clangd each declare their flags as global llvm::cl::opt objects, built by static
// initializers, and LLVM aborts when two of them register the same flag ("fallback-style" is in clang-format and
// clang-tidy). smith links all three, so their initializers must not run at load. This renames the object's
// __DATA,__mod_init_func section to __DATA,__smith_<name> and makes it plain data that the linker keeps
// (S_ATTR_NO_DEAD_STRIP), so that the shim for the tool (shim/tool.h) runs the pointers in it when that tool is the one
// that runs. A 64-bit Mach-O object (what clang makes for arm64 and x86_64) is the only input; it is read whole into a
// fixed buffer, patched in place and written to the new file. Prints how many sections it changed.
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// On in every build. A failure is a bug or a bad input: report where and stop.
#define DEFER_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "defer_init: %s failed at %s:%d\n", #condition, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

constexpr uint32_t defer_magic_64 = 0xFEEDFACFU;
constexpr uint32_t defer_segment_64 = 0x19;
constexpr uint32_t defer_no_dead_strip = 0x10000000U; // S_ATTR_NO_DEAD_STRIP on a regular section
constexpr size_t defer_object_max = 64U << 20;
constexpr size_t defer_header_size = 32;
constexpr size_t defer_segment_header_size = 72;
constexpr size_t defer_section_size = 80;
constexpr size_t defer_name_size = 16;
constexpr size_t defer_flags_offset = 64;    // in a section header
constexpr size_t defer_sections_offset = 64; // in a segment command: where nsects is
constexpr uint32_t defer_sections_max = 1U << 12;
constexpr uint32_t defer_commands_max = 1U << 16;

static unsigned char defer_object[defer_object_max];

static uint32_t defer_read(size_t offset) {
    DEFER_ASSERT(offset + sizeof(uint32_t) <= defer_object_max);
    uint32_t value;
    memcpy(&value, defer_object + offset, sizeof value);
    return value;
}

static void defer_write(size_t offset, uint32_t value) {
    DEFER_ASSERT(offset + sizeof value <= defer_object_max);
    memcpy(defer_object + offset, &value, sizeof value);
}

// Patches the section at `section` if it is __mod_init_func; returns whether it did.
static bool defer_section(size_t section, const char *name) {
    static const char original[] = "__mod_init_func";
    if (memcmp(defer_object + section, original, sizeof original) != 0) return false;
    memset(defer_object + section, 0, defer_name_size);
    memcpy(defer_object + section, name, strlen(name));
    defer_write(section + defer_flags_offset, defer_no_dead_strip); // type 0: regular data
    return true;
}

// Patches every __mod_init_func of the object and returns how many there were.
static uint32_t defer_object_sections(size_t size, const char *name) {
    DEFER_ASSERT(size >= defer_header_size && defer_read(0) == defer_magic_64);
    uint32_t commands = defer_read(16);
    DEFER_ASSERT(commands <= defer_commands_max);
    size_t offset = defer_header_size;
    uint32_t patched = 0;
    for (uint32_t i = 0; i < commands; i++) { // bounded by commands
        DEFER_ASSERT(offset + 8 <= size);
        uint32_t command = defer_read(offset);
        uint32_t length = defer_read(offset + 4);
        DEFER_ASSERT(length >= 8 && offset + length <= size);
        if (command == defer_segment_64) {
            uint32_t sections = defer_read(offset + defer_sections_offset);
            DEFER_ASSERT(sections <= defer_sections_max);
            DEFER_ASSERT(defer_segment_header_size + (size_t)sections * defer_section_size <= length);
            for (uint32_t s = 0; s < sections; s++) // bounded by sections
                patched += defer_section(offset + defer_segment_header_size + (size_t)s * defer_section_size, name);
        }
        offset += length;
    }
    return patched;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: defer_init in.o out.o name\n");
        return 2;
    }
    char section[defer_name_size + 1];
    int written = snprintf(section, sizeof section, "__smith_%s", argv[3]);
    DEFER_ASSERT(written > 0 && (size_t)written <= defer_name_size);
    FILE *in = fopen(argv[1], "rb");
    DEFER_ASSERT(in != nullptr);
    size_t size = fread(defer_object, 1, defer_object_max, in);
    DEFER_ASSERT(ferror(in) == 0 && feof(in) != 0); // the whole file fit
    fclose(in);
    uint32_t patched = defer_object_sections(size, section);
    FILE *out = fopen(argv[2], "wb");
    DEFER_ASSERT(out != nullptr);
    DEFER_ASSERT(fwrite(defer_object, 1, size, out) == size);
    DEFER_ASSERT(fclose(out) == 0);
    printf("%s: %" PRIu32 " initializer section(s) deferred as %s\n", argv[1], patched, section);
    return 0;
}
