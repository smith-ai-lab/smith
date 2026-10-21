// LLVM's symbolizer, behind a C interface: the one place smith turns an address in a binary into a function, a file and
// a line, with the inlined frames. `smith symbolize`, the test report and the llvm-symbolizer protocol all call it
// (runtime/symbolize.c).
//
// Where the DWARF is. On macOS the linker leaves it in the object files, and the binary holds only a debug map: stab
// entries (N_OSO names an object file, and N_FUN gives each function's address and size). Two ways to read it:
//   1. a dSYM, which `dsymutil` makes by reading every object and linking its DWARF: LLVM's symbolizer finds one beside
//      the binary, and checks its UUID;
//   2. the debug map itself: find the function an address falls in, find the same symbol in its object file, and ask
//   the
//      symbolizer about the object, which has its own DWARF.
// smith reads the debug map (the second): the objects of a test build are in the build folder when a test fails,
// nothing has to be made first, and a failure costs the DWARF of one object, not of the program (see the measurements
// in docs/build-c.md). A dSYM, when one is there and matches, is used first; an object that is gone or has changed
// since the link (its modification time is in the N_OSO entry) is reported, never read.
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/MachO.h"
#include "llvm/Support/Error.h"

extern "C" {
// Keep in step with runtime/symbolize.c.
typedef struct {
    char function[512];
    char file[1024];
    uint32_t line;
    uint32_t column;
} SmithSymbol;
}

namespace {

llvm::symbolize::LLVMSymbolizer &symbolizer() {
    static llvm::symbolize::LLVMSymbolizer instance = [] {
        llvm::symbolize::LLVMSymbolizer::Options options;
        options.PrintFunctions = llvm::symbolize::FunctionNameKind::LinkageName;
        options.Demangle = true;
        options.UseSymbolTable = true;
#if defined(__aarch64__)
        options.DefaultArch = "arm64";
#elif defined(__x86_64__)
        options.DefaultArch = "x86_64";
#endif
        return llvm::symbolize::LLVMSymbolizer(options);
    }();
    return instance;
}

std::string last_problem;

struct DebugEntry {
    uint64_t address;
    uint64_t size;
    uint32_t object;
    std::string name;
};

struct DebugMap {
    std::vector<std::string> objects;
    std::vector<uint64_t> times; // the modification time each N_OSO recorded
    std::vector<DebugEntry> entries;
    bool valid = false;
};

struct ObjectSymbols {
    std::map<std::string, uint64_t> values;
    bool valid = false;
};

const char *kBad = llvm::DILineInfo::BadString;

// The binary's symbol table as a sequence of nlists, from a Mach-O file's bytes. Returns false when it is not one.
bool symbol_table(const llvm::object::MachOObjectFile &object, const char *&strings, size_t &strings_size,
                  const char *&nlists, uint32_t &count) {
    if (!object.is64Bit()) return false;
    llvm::MachO::symtab_command table = object.getSymtabLoadCommand();
    llvm::StringRef data = object.getData();
    if (table.nsyms == 0 ||
        (uint64_t)table.symoff + (uint64_t)table.nsyms * sizeof(llvm::MachO::nlist_64) > data.size() ||
        (uint64_t)table.stroff + table.strsize > data.size())
        return false;
    nlists = data.data() + table.symoff;
    strings = data.data() + table.stroff;
    strings_size = table.strsize;
    count = table.nsyms;
    return true;
}

const char *string_at(const char *strings, size_t size, uint32_t index) {
    return index < size ? strings + index : "";
}

llvm::MachO::nlist_64 nlist_at(const char *nlists, uint32_t index) {
    llvm::MachO::nlist_64 entry;
    std::memcpy(&entry, nlists + (size_t)index * sizeof entry, sizeof entry);
    return entry;
}

// A Mach-O file kept open with its buffer.
struct Opened {
    llvm::object::OwningBinary<llvm::object::Binary> owner;
    const llvm::object::MachOObjectFile *object = nullptr;
};

bool open_file(const char *path, Opened &opened) {
    auto binary = llvm::object::createBinary(path);
    if (!binary) {
        llvm::consumeError(binary.takeError());
        return false;
    }
    opened.owner = std::move(*binary);
    opened.object = llvm::dyn_cast<llvm::object::MachOObjectFile>(opened.owner.getBinary());
    return opened.object != nullptr;
}

DebugMap read_debug_map(const char *path) {
    DebugMap map;
    Opened opened;
    if (!open_file(path, opened)) return map;
    const char *strings = nullptr;
    const char *nlists = nullptr;
    size_t strings_size = 0;
    uint32_t count = 0;
    if (!symbol_table(*opened.object, strings, strings_size, nlists, count)) return map;
    int64_t current = -1;
    std::string pending_name;
    uint64_t pending_address = 0;
    bool pending = false;
    for (uint32_t i = 0; i < count; i++) {
        llvm::MachO::nlist_64 entry = nlist_at(nlists, i);
        if ((entry.n_type & llvm::MachO::N_STAB) == 0) continue;
        const char *name = string_at(strings, strings_size, entry.n_strx);
        if (entry.n_type == llvm::MachO::N_OSO) {
            map.objects.emplace_back(name);
            map.times.push_back(entry.n_value);
            current = (int64_t)map.objects.size() - 1;
            pending = false;
        } else if (entry.n_type == llvm::MachO::N_FUN && current >= 0) {
            if (name[0] != '\0') {
                pending_name = name;
                pending_address = entry.n_value;
                pending = true;
            } else if (pending) {
                map.entries.push_back({pending_address, entry.n_value, (uint32_t)current, pending_name});
                pending = false;
            }
        }
    }
    std::sort(map.entries.begin(), map.entries.end(),
              [](const DebugEntry &a, const DebugEntry &b) { return a.address < b.address; });
    map.valid = true;
    return map;
}

ObjectSymbols read_object_symbols(const std::string &path) {
    ObjectSymbols symbols;
    Opened opened;
    if (!open_file(path.c_str(), opened)) return symbols;
    const char *strings = nullptr;
    const char *nlists = nullptr;
    size_t strings_size = 0;
    uint32_t count = 0;
    if (!symbol_table(*opened.object, strings, strings_size, nlists, count)) return symbols;
    for (uint32_t i = 0; i < count; i++) {
        llvm::MachO::nlist_64 entry = nlist_at(nlists, i);
        if ((entry.n_type & llvm::MachO::N_STAB) != 0 || (entry.n_type & llvm::MachO::N_TYPE) != llvm::MachO::N_SECT)
            continue;
        symbols.values.emplace(string_at(strings, strings_size, entry.n_strx), entry.n_value);
    }
    symbols.valid = true;
    return symbols;
}

// The last binary's debug map, and the symbols of the objects it names, kept between calls.
std::string cached_path;
DebugMap cached_map;
std::map<std::string, ObjectSymbols> cached_objects;

const DebugMap &debug_map(const char *path) {
    if (cached_path != path) {
        cached_path = path;
        cached_map = read_debug_map(path);
        cached_objects.clear();
    }
    return cached_map;
}

int fill(const llvm::DIInliningInfo &info, SmithSymbol *out, int capacity) {
    int count = 0;
    for (uint32_t i = 0; i < info.getNumberOfFrames() && count < capacity; i++) {
        const llvm::DILineInfo &frame = info.getFrame(i);
        SmithSymbol &symbol = out[count++];
        std::snprintf(symbol.function, sizeof symbol.function, "%s",
                      frame.FunctionName == kBad ? "" : frame.FunctionName.c_str());
        std::snprintf(symbol.file, sizeof symbol.file, "%s", frame.FileName == kBad ? "" : frame.FileName.c_str());
        symbol.line = frame.Line;
        symbol.column = frame.Column;
    }
    return count;
}

bool has_lines(const llvm::DIInliningInfo &info) {
    return info.getNumberOfFrames() > 0 && info.getFrame(0).FileName != kBad && info.getFrame(0).Line != 0;
}

int symbolize_with(const std::string &module, uint64_t address, SmithSymbol *out, int capacity) {
    auto result = symbolizer().symbolizeInlinedCode(module, {address, llvm::object::SectionedAddress::UndefSection});
    if (!result) {
        llvm::consumeError(result.takeError());
        return 0;
    }
    return has_lines(*result) ? fill(*result, out, capacity) : 0;
}

// The debug map: the object the address's function came from, and the address in it.
int symbolize_debug_map(const char *binary, uint64_t address, SmithSymbol *out, int capacity) {
    const DebugMap &map = debug_map(binary);
    if (!map.valid || map.entries.empty()) return 0;
    auto after = std::upper_bound(map.entries.begin(), map.entries.end(), address,
                                  [](uint64_t value, const DebugEntry &entry) { return value < entry.address; });
    if (after == map.entries.begin()) return 0;
    const DebugEntry &entry = *(after - 1);
    if (entry.size != 0 && address >= entry.address + entry.size) return 0;
    const std::string &object = map.objects[entry.object];
    if (object.find('(') != std::string::npos) {
        last_problem = "the function is in an archive member (" + object + "), which is not read";
        return 0;
    }
    struct stat info;
    if (stat(object.c_str(), &info) != 0) {
        last_problem = "the object file " + object + " is gone";
        return 0;
    }
    if (map.times[entry.object] != 0 && (uint64_t)info.st_mtime != map.times[entry.object]) {
        last_problem = "the object file " + object + " has changed since the program was linked";
        return 0;
    }
    auto found = cached_objects.find(object);
    if (found == cached_objects.end()) found = cached_objects.emplace(object, read_object_symbols(object)).first;
    auto value = found->second.values.find(entry.name);
    if (!found->second.valid || value == found->second.values.end()) return 0;
    return symbolize_with(object, value->second + (address - entry.address), out, capacity);
}

} // namespace

extern "C" {

// The Mach-O UUID of a file as XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX; false when it has none.
bool smith_symbolize_uuid(const char *path, char out[37]) {
    Opened opened;
    if (!open_file(path, opened)) return false;
    for (const auto &command : opened.object->load_commands()) {
        if (command.C.cmd != llvm::MachO::LC_UUID) continue;
        llvm::MachO::uuid_command uuid = opened.object->getUuidCommand(command);
        static const char digits[] = "0123456789ABCDEF";
        size_t at = 0;
        for (size_t i = 0; i < 16; i++) {
            if (i == 4 || i == 6 || i == 8 || i == 10) out[at++] = '-';
            out[at++] = digits[uuid.uuid[i] >> 4];
            out[at++] = digits[uuid.uuid[i] & 0xF];
        }
        out[at] = '\0';
        return true;
    }
    return false;
}

// The address __TEXT is linked at (0x100000000 for a program); 0 when the file has none.
uint64_t smith_symbolize_text_base(const char *path) {
    Opened opened;
    if (!open_file(path, opened)) return 0;
    for (const auto &command : opened.object->load_commands()) {
        if (command.C.cmd != llvm::MachO::LC_SEGMENT_64) continue;
        llvm::MachO::segment_command_64 segment = opened.object->getSegment64LoadCommand(command);
        if (std::strncmp(segment.segname, "__TEXT", sizeof segment.segname) == 0) return segment.vmaddr;
    }
    return 0;
}

// Why the last lookup found nothing in a place that should have had the answer; "" when nothing is known.
const char *smith_symbolize_problem(void) {
    return last_problem.c_str();
}

// The frames at an address of the binary as linked (a link address, not a load address), innermost first: a dSYM that
// matches the binary, else the debug map, else (no line info) the name from the symbol table alone. Returns how many
// frames, 0 when the binary has nothing for the address.
int smith_symbolize_code(const char *binary, uint64_t address, SmithSymbol *out, int capacity) {
    last_problem.clear();
    int count = symbolize_with(binary, address, out, capacity);
    if (count > 0) return count;
    count = symbolize_debug_map(binary, address, out, capacity);
    if (count > 0) return count;
    auto named = symbolizer().symbolizeCode(binary, {address, llvm::object::SectionedAddress::UndefSection});
    if (!named) {
        llvm::consumeError(named.takeError());
        return 0;
    }
    if (named->FunctionName == kBad || named->FunctionName.empty()) return 0;
    std::snprintf(out[0].function, sizeof out[0].function, "%s", named->FunctionName.c_str());
    out[0].file[0] = '\0';
    out[0].line = 0;
    out[0].column = 0;
    return 1;
}

} // extern "C"
