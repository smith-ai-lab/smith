// What smith does differently on macOS, one function each; runtime/platform_linux.c has the same functions for Linux
// and knowledge/platform-layer.md the table of what differs. smith.c includes exactly one of the two, after
// smith_die, and the files it includes after that (the executor, the symbolizer, explain) call these by name.
#include <compression.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/clonefile.h>
#include <sys/proc_info.h>

constexpr uint32_t smith_platform_stopped = 4; // SSTOP of <sys/proc.h>, a kernel header
constexpr size_t smith_platform_id_bytes = 16; // an LC_UUID, as an ELF build-id of smith's link is on Linux

// The hex of the running executable's LC_UUID, which the linker derives from its contents: the same smith is the same
// name, another build another.
static void smith_platform_self_id(char *out, size_t size) {
    SMITH_ASSERT(size >= 2 * smith_platform_id_bytes + 1);
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    SMITH_ASSERT(header != nullptr && header->magic == MH_MAGIC_64);
    const struct load_command *command = (const struct load_command *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) { // bounded by the header
        if (command->cmd == LC_UUID) {
            const struct uuid_command *uuid = (const struct uuid_command *)command;
            for (size_t k = 0; k < sizeof uuid->uuid; k++)
                snprintf(out + 2 * k, 3, "%02x", uuid->uuid[k]);
            return;
        }
        command = (const struct load_command *)((const char *)command + command->cmdsize);
    }
    smith_die(smith_exit_usage, "this smith has no LC_UUID", nullptr);
}

// The LC_UUID of the Mach-O file `path` as hex, from its first 16 KB; false when it is no thin Mach-O with one.
static bool smith_platform_file_id(const char *path, char *out) {
    int file = open(path, O_RDONLY);
    if (file < 0) return false;
    unsigned char buffer[16384];
    ssize_t count = read(file, buffer, sizeof buffer);
    close(file);
    if (count < (ssize_t)sizeof(struct mach_header_64)) return false;
    const struct mach_header_64 *header = (const struct mach_header_64 *)buffer;
    if (header->magic != MH_MAGIC_64) return false;
    size_t at = sizeof *header;
    for (uint32_t i = 0; i < header->ncmds; i++) { // bounded by the header
        if (at + sizeof(struct load_command) > (size_t)count) return false;
        const struct load_command *command = (const struct load_command *)(buffer + at);
        if (command->cmd == LC_UUID && at + sizeof(struct uuid_command) <= (size_t)count) {
            const struct uuid_command *uuid = (const struct uuid_command *)command;
            for (size_t k = 0; k < sizeof uuid->uuid; k++)
                snprintf(out + 2 * k, 3, "%02x", uuid->uuid[k]);
            return true;
        }
        if (command->cmdsize < sizeof *command) return false;
        at += command->cmdsize;
    }
    return false;
}

// Makes `to` a program that is `from`: a clone (APFS copy on write), else a hard link, else a symbolic link (the
// programs folder in smith.c says why a clone comes first). False when none can be made.
static bool smith_platform_clone(const char *from, const char *to) {
    return clonefile(from, to, 0) == 0 || link(from, to) == 0 || symlink(from, to) == 0;
}

// The monotonic clock in nanoseconds.
[[maybe_unused]] static unsigned long long smith_platform_monotonic_ns(void) {
    return clock_gettime_nsec_np(CLOCK_MONOTONIC);
}

// When the file was last changed.
static struct timespec smith_platform_mtime(const struct stat *info) {
    return info->st_mtimespec;
}

// 1 when the process is stopped (SIGSTOP or a debugger), 0 when it runs, -1 when it cannot be read (it is gone).
// Only a smith with lldb calls it (runtime/explain.c).
[[maybe_unused]] static int smith_platform_process_stopped(long pid) {
    struct proc_bsdshortinfo info = {};
    if (proc_pidinfo((int)pid, PROC_PIDT_SHORTBSDINFO, 0, &info, sizeof info) != (int)sizeof info) return -1;
    return info.pbsi_status == smith_platform_stopped ? 1 : 0;
}

static unsigned char smith_platform_scratch[1U << 22]; // for libcompression: LZFSE needs a fraction of it

// Decompresses a file of the resource pack (scripts/pack.c compresses with LZFSE) into out; returns its size.
static size_t smith_platform_decompress(unsigned char *out, size_t capacity, const unsigned char *in, size_t size) {
    SMITH_ASSERT(compression_decode_scratch_buffer_size(COMPRESSION_LZFSE) <= sizeof smith_platform_scratch);
    return compression_decode_buffer(out, capacity, in, size, smith_platform_scratch, COMPRESSION_LZFSE);
}
