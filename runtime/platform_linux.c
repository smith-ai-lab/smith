// What smith does differently on Linux, one function each; runtime/platform_macos.c has the same functions for macOS
// and knowledge/platform-layer.md the table of what differs. smith.c includes exactly one of the two, after
// smith_die, and the files it includes after that (the executor, the symbolizer, explain) call these by name.
//
// smith's identity on Linux is the ELF note NT_GNU_BUILD_ID, which build.c asks lld for as an MD5 of the output
// (`--build-id=md5`): 16 bytes, the size of a Mach-O LC_UUID, so a programs folder has a name of the same length.
#include <elf.h>
#include <linux/fs.h>
#include <sys/ioctl.h>

constexpr size_t smith_platform_elf_max = 1U << 16; // the headers and notes are in the first pages of an executable
constexpr size_t smith_platform_id_bytes = 16;
constexpr size_t smith_platform_notes_max = 4096; // notes in one PT_NOTE segment

static unsigned char smith_platform_elf[smith_platform_elf_max];

// The build-id note of the PT_NOTE segment at [at, end) of the buffer as hex; false when it holds none of 16 bytes.
static bool smith_platform_note_id(size_t at, size_t end, char *out) {
    for (size_t i = 0; i < smith_platform_notes_max && at + sizeof(Elf64_Nhdr) <= end; i++) { // bounded by the count
        Elf64_Nhdr note;
        memcpy(&note, smith_platform_elf + at, sizeof note);
        size_t name = at + sizeof note;
        size_t descriptor = name + ((note.n_namesz + 3U) & ~3U);
        size_t next = descriptor + ((note.n_descsz + 3U) & ~3U);
        if (next > end || next <= at) return false;
        if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && memcmp(smith_platform_elf + name, "GNU", 4) == 0 &&
            note.n_descsz == smith_platform_id_bytes) {
            for (size_t k = 0; k < smith_platform_id_bytes; k++)
                snprintf(out + 2 * k, 3, "%02x", smith_platform_elf[descriptor + k]);
            return true;
        }
        at = next;
    }
    return false;
}

// The build-id of the 64-bit ELF file `path` as hex, from its first 64 KB; false when it has none of 16 bytes.
static bool smith_platform_file_id(const char *path, char *out) {
    int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0) return false;
    ssize_t count = read(file, smith_platform_elf, sizeof smith_platform_elf);
    close(file);
    Elf64_Ehdr header;
    if (count < (ssize_t)sizeof header) return false;
    memcpy(&header, smith_platform_elf, sizeof header);
    if (memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 || header.e_ident[EI_CLASS] != ELFCLASS64) return false;
    if (header.e_phentsize != sizeof(Elf64_Phdr)) return false;
    for (size_t i = 0; i < header.e_phnum; i++) { // bounded by the header's count, at most 65535
        size_t at = header.e_phoff + i * sizeof(Elf64_Phdr);
        if (at + sizeof(Elf64_Phdr) > (size_t)count) return false;
        Elf64_Phdr segment;
        memcpy(&segment, smith_platform_elf + at, sizeof segment);
        if (segment.p_type != PT_NOTE || segment.p_offset + segment.p_filesz > (size_t)count) continue;
        if (smith_platform_note_id(segment.p_offset, segment.p_offset + segment.p_filesz, out)) return true;
    }
    return false;
}

// The hex of the running executable's build-id: the same smith is the same name, another build another.
static void smith_platform_self_id(char *out, size_t size) {
    SMITH_ASSERT(size >= 2 * smith_platform_id_bytes + 1);
    if (!smith_platform_file_id("/proc/self/exe", out))
        smith_die(smith_exit_usage, "this smith has no build-id of 16 bytes (link it with --build-id=md5)", nullptr);
}

// Makes `to` a program that is `from`: a clone (FICLONE, on a file system with copy on write: btrfs, XFS), else a hard
// link, else a symbolic link, as on macOS. False when none can be made.
static bool smith_platform_clone(const char *from, const char *to) {
    int source = open(from, O_RDONLY | O_CLOEXEC);
    if (source >= 0) {
        int target = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0755);
        bool cloned = target >= 0 && ioctl(target, FICLONE, source) == 0;
        if (target >= 0) close(target);
        if (target >= 0 && !cloned) unlink(to);
        close(source);
        if (cloned) return true;
    }
    return link(from, to) == 0 || symlink(from, to) == 0;
}

// The monotonic clock in nanoseconds.
[[maybe_unused]] static unsigned long long smith_platform_monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000000000ULL + (unsigned long long)now.tv_nsec;
}

// When the file was last changed.
static struct timespec smith_platform_mtime(const struct stat *info) {
    return info->st_mtim;
}

// 1 when the process is stopped (SIGSTOP or a debugger), 0 when it runs, -1 when it cannot be read (it is gone): the
// state letter of /proc/<pid>/stat, after the command's closing parenthesis. Only a smith with lldb calls it
// (runtime/explain.c).
[[maybe_unused]] static int smith_platform_process_stopped(long pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%ld/stat", pid);
    int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0) return -1;
    char text[512];
    ssize_t count = read(file, text, sizeof text - 1);
    close(file);
    if (count <= 0) return -1;
    text[count] = '\0';
    const char *close_paren = strrchr(text, ')');
    if (close_paren == nullptr || close_paren[1] != ' ') return -1;
    char state = close_paren[2];
    return state == 'T' || state == 't' ? 1 : 0;
}

// The resource pack is stored uncompressed on Linux (scripts/pack.c compresses only on macOS, with Apple's
// libcompression), so every file of it has its stored size equal to its size and nothing calls this; 0 says so.
static size_t smith_platform_decompress(unsigned char *out, size_t capacity, const unsigned char *in, size_t size) {
    (void)out;
    (void)capacity;
    (void)in;
    (void)size;
    return 0;
}
