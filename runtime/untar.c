// tar extraction in C: smith's own reader, so `smith_package` runs no other process. Included by runtime/package.c
// after runtime/gzip.c and runtime/xz.c, whose decoders push their output to smith_untar_feed.
//
// Formats: a .tar, .tar.gz (also .tgz), .tar.xz, or a .gz of one file that is no tar, told apart by their first bytes,
// never by the name. The tar may be ustar (with its prefix field), GNU (long names, 'L' and 'K') or pax (extended
// headers with path, linkpath, size and mtime; other keys are ignored). Entries are regular files, folders, symlinks
// and hard links; a device, a FIFO, a sparse file or any other type is an error that names it. zip, bzip2 and zstd are
// refused by name.
//
// What it will not do, as bsdtar does not, and every one is an error that names the entry and the reason:
//   - write outside the folder it was given: absolute names and any ".." are refused, a path is never followed through
//     a symlink (every folder on the way is opened with O_NOFOLLOW, so a symlink made earlier in the archive cannot
//     redirect a later entry), and a symlink must point inside the package: its target is relative and has no ".."
//     after a name (so it cannot be steered by another symlink), and no more ".." at the start than its folder is deep;
//   - make a device, a FIFO or a setuid file: the mode is kept with the owner, group and other bits only, and the
//     process umask applies, as it does to tar run by an ordinary user.
// File modes and modification times are kept. Folders are made owner-writable whatever the archive says, so that
// the package can be removed; folder times are not restored.
//
// The tar is read as a stream of 512-byte blocks pushed by the decompressor in pieces of any size, as a state machine,
// so a 2 GB tar never exists in memory. An archive must end with its two zero blocks; a stream that ends inside an
// entry, or with data after the end, is an error.
#include <sys/mman.h>

constexpr size_t smith_untar_block = 512;
constexpr size_t smith_untar_extension_max = 65536; // pax and GNU long-name data
constexpr size_t smith_untar_depth_max = 64;        // folders below the extraction folder
constexpr size_t smith_untar_name_max = 255;
constexpr int smith_untar_write_retries = 1000;

typedef enum {
    SmithUntar_Header,
    SmithUntar_Sniff,     // the first bytes of a "._" file, which may be macOS metadata, not yet created
    SmithUntar_File,      // the data of a regular file, written as it arrives
    SmithUntar_Extension, // the data of a pax or GNU long-name entry, collected
    SmithUntar_Skip,      // data and padding that are dropped
    SmithUntar_Ended,     // after the two zero blocks: only zeros may follow
} SmithUntarPhase;

typedef struct {
    SmithPackageFault *error;
    int root; // the extraction folder
    SmithUntarPhase phase;
    uint64_t offset;        // bytes fed so far, for messages
    uint64_t header_offset; // where the header being read starts
    uint8_t header[smith_untar_block];
    size_t filled;
    uint64_t remaining; // bytes left in the phase
    uint64_t file_size; // of the regular file being written, to find its padding
    uint64_t mode;      // of a "._" file that is not created yet
    uint8_t sniff[4];   // its first bytes
    size_t sniffed;
    unsigned zero_blocks;
    int file;            // the open regular file, or -1
    char name[PATH_MAX]; // the current entry, normalised
    int64_t mtime_seconds;
    long mtime_nanoseconds;
    char extension_kind; // 'x', 'L' or 'K' while collecting
    size_t extension_used;
    uint8_t extension[smith_untar_extension_max];
    bool has_path; // overrides from pax or GNU headers, for the next entry only
    bool has_link;
    bool has_size;
    bool has_mtime;
    char path[PATH_MAX];
    char link[PATH_MAX];
    uint64_t size;
    int64_t pax_seconds;
    long pax_nanoseconds;
    int chain_fd[smith_untar_depth_max]; // the open folders of the last entry's parent
    size_t chain_depth;
    char chain_path[PATH_MAX];
    size_t chain_length;
    char climbing[PATH_MAX]; // the first symlink that leaves its top-level folder, else ""
} SmithUntar;

static bool smith_untar_reject(SmithUntar *untar, const char *reason) {
    return smith_package_fail(untar->error, "entry \"%.200s\": %s", untar->name, reason);
}

static bool smith_untar_system(SmithUntar *untar, const char *what) {
    return smith_package_fail(untar->error, "entry \"%.200s\": cannot %s: %s", untar->name, what, strerror(errno));
}

// A tar number: octal digits ended by a NUL or a space, or (high bit set) big-endian binary. False when damaged.
static bool smith_untar_number(const uint8_t *field, size_t length, uint64_t *value) {
    uint64_t result = 0;
    if ((field[0] & 0x80U) != 0) {
        if ((field[0] & 0x40U) != 0) return false; // negative
        result = field[0] & 0x3FU;
        for (size_t i = 1; i < length; i++) {
            if (result >> 55 != 0) return false;
            result = (result << 8) | field[i];
        }
        *value = result;
        return true;
    }
    size_t at = 0;
    while (at < length && field[at] == ' ')
        at++;
    size_t digits = 0;
    for (; at < length && field[at] >= '0' && field[at] <= '7'; at++, digits++) {
        if (result >> 60 != 0) return false;
        result = (result << 3) | (uint64_t)(field[at] - '0');
    }
    if (digits == 0 || (at < length && field[at] != 0 && field[at] != ' ')) return false;
    *value = result;
    return true;
}

static bool smith_untar_header_checksum_ok(const uint8_t *header) {
    uint64_t wanted = 0;
    if (!smith_untar_number(header + 148, 8, &wanted)) return false;
    uint64_t unsigned_sum = 0;
    int64_t signed_sum = 0;
    for (size_t i = 0; i < smith_untar_block; i++) {
        uint8_t byte = i >= 148 && i < 156 ? ' ' : header[i];
        unsigned_sum += byte;
        signed_sum += (int8_t)byte;
    }
    return wanted == unsigned_sum || (int64_t)wanted == signed_sum;
}

static bool smith_untar_is_zero(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (data[i] != 0) return false;
    }
    return true;
}

// Normalises an entry name into `out`: "." parts and repeated slashes are dropped, a trailing slash is dropped. Returns
// nullptr, or the reason the name is refused. An empty `out` is the extraction folder itself.
static const char *smith_untar_clean(const char *raw, char *out, size_t *components) {
    if (raw[0] == '/') return "the name is an absolute path";
    size_t length = 0;
    *components = 0;
    for (const char *at = raw; *at != '\0';) {
        const char *end = at;
        while (*end != '/' && *end != '\0')
            end++;
        size_t part = (size_t)(end - at);
        if (part == 2 && at[0] == '.' && at[1] == '.') return "the name goes up out of the package (..)";
        if (part > 0 && !(part == 1 && at[0] == '.')) {
            if (length + part + 1 >= PATH_MAX) return "the name is too long";
            if (length > 0) out[length++] = '/';
            memcpy(out + length, at, part);
            length += part;
            (*components)++;
        }
        at = *end == '/' ? end + 1 : end;
    }
    out[length] = '\0';
    return *components > smith_untar_depth_max ? "the name is nested too deeply" : nullptr;
}

// Whether a symlink at `link` (normalised) to `target` stays inside the extraction folder. Returns nullptr, or the
// reason it does not. `climbs` is set when it reaches the folder that holds the top-level entries or above, which
// matters when the package turns out to be one top-level folder: the package is that folder's contents.
static const char *smith_untar_check_target(const char *link, const char *target, bool *climbs) {
    if (target[0] == '\0') return "the link has an empty target";
    if (target[0] == '/') return "the link's target is an absolute path";
    size_t depth = 0;
    for (const char *at = link; *at != '\0'; at++) {
        if (*at == '/') depth++;
    }
    size_t up = 0;
    bool named = false;
    for (const char *at = target; *at != '\0';) {
        const char *end = at;
        while (*end != '/' && *end != '\0')
            end++;
        size_t part = (size_t)(end - at);
        if (part == 2 && at[0] == '.' && at[1] == '.') {
            if (named) return "the link's target goes up after going down, so it cannot be shown to stay inside";
            up++;
        } else if (part > 0 && !(part == 1 && at[0] == '.')) {
            named = true;
        }
        at = *end == '/' ? end + 1 : end;
    }
    if (up > depth) return "the link's target points outside the package";
    if (up >= depth) *climbs = true;
    return nullptr;
}

static bool smith_untar_write(int fd, const uint8_t *data, size_t size) {
    size_t tries = 0;
    while (size > 0) {
        ssize_t wrote = write(fd, data, size);
        if (wrote < 0 && errno == EINTR && ++tries < smith_untar_write_retries) continue;
        if (wrote <= 0) return false;
        data += wrote;
        size -= (size_t)wrote;
    }
    return true;
}

static int smith_untar_open_folder(int parent, const char *name) {
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0 || errno != ENOENT) return fd;
    if (mkdirat(parent, name, 0777) != 0 && errno != EEXIST) return -1;
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static void smith_untar_close_chain(SmithUntar *untar, size_t keep) {
    for (size_t i = keep; i < untar->chain_depth; i++)
        close(untar->chain_fd[i]);
    untar->chain_depth = keep;
}

// Opens the folders of `parent` (normalised, no trailing slash; empty is the extraction folder) from the root, making
// the missing ones, never through a symlink. The folders the last entry used stay open and are reused, since archives
// list a folder's entries together. Returns the folder's descriptor in `fd`.
static bool smith_untar_enter(SmithUntar *untar, const char *parent, size_t length, int *fd) {
    const char *cached = untar->chain_path;
    size_t same = 0;
    size_t limit = length < untar->chain_length ? length : untar->chain_length;
    while (same < limit && parent[same] == cached[same])
        same++;
    while (same > 0 &&
           !((same == untar->chain_length || cached[same] == '/') && (same == length || parent[same] == '/')))
        same--;
    size_t keep = 0;
    for (size_t i = 0; i < same; i++) {
        if (parent[i] == '/') keep++;
    }
    smith_untar_close_chain(untar, same > 0 ? keep + 1 : 0);
    size_t at = same > 0 ? same + 1 : 0;
    for (size_t part = 0; at < length && part < smith_untar_depth_max; part++) {
        size_t end = at;
        while (end < length && parent[end] != '/')
            end++;
        char name[smith_untar_name_max + 1];
        if (end - at > smith_untar_name_max) return smith_untar_reject(untar, "a name in the path is too long");
        memcpy(name, parent + at, end - at);
        name[end - at] = '\0';
        int up = untar->chain_depth == 0 ? untar->root : untar->chain_fd[untar->chain_depth - 1];
        int opened = smith_untar_open_folder(up, name);
        if (opened < 0) {
            smith_untar_close_chain(untar, 0);
            untar->chain_length = 0;
            if (errno == ELOOP || errno == ENOTDIR) {
                return smith_untar_reject(untar, "the path goes through a symlink or a file, not a folder");
            }
            return smith_untar_system(untar, "make a folder on the way");
        }
        untar->chain_fd[untar->chain_depth++] = opened;
        at = end + 1;
    }
    memcpy(untar->chain_path, parent, length);
    untar->chain_path[length] = '\0';
    untar->chain_length = length;
    *fd = untar->chain_depth == 0 ? untar->root : untar->chain_fd[untar->chain_depth - 1];
    return true;
}

// The folder that holds the current entry and the entry's own name in it.
static bool smith_untar_parent(SmithUntar *untar, int *fd, const char **leaf) {
    char *slash = strrchr(untar->name, '/');
    if (slash == nullptr) {
        *leaf = untar->name;
        return smith_untar_enter(untar, "", 0, fd);
    }
    *leaf = slash + 1;
    return smith_untar_enter(untar, untar->name, (size_t)(slash - untar->name), fd);
}

static void smith_untar_times(SmithUntar *untar, int fd) {
    struct timespec times[2] = {{.tv_nsec = UTIME_OMIT},
                                {.tv_sec = (time_t)untar->mtime_seconds, .tv_nsec = untar->mtime_nanoseconds}};
    futimens(fd, times); // best effort: a time that cannot be set does not fail the package
}

// Where the data of an entry ends, the block padding follows.
static void smith_untar_after_data(SmithUntar *untar, uint64_t size) {
    uint64_t padding = (smith_untar_block - size % smith_untar_block) % smith_untar_block;
    untar->phase = padding > 0 ? SmithUntar_Skip : SmithUntar_Header;
    untar->remaining = padding;
}

static bool smith_untar_file_done(SmithUntar *untar) {
    smith_untar_times(untar, untar->file);
    int fd = untar->file;
    untar->file = -1;
    if (close(fd) != 0) return smith_untar_system(untar, "close the file");
    smith_untar_after_data(untar, untar->file_size);
    return true;
}

static bool smith_untar_create(SmithUntar *untar, uint64_t mode, uint64_t size) {
    int dir = -1;
    const char *leaf = nullptr;
    if (!smith_untar_parent(untar, &dir, &leaf)) return false;
    int flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC;
    mode_t permissions = (mode_t)(mode & 0777U);
    int fd = openat(dir, leaf, flags, permissions);
    if (fd < 0 && errno == EEXIST) { // a later entry replaces an earlier one
        if (unlinkat(dir, leaf, 0) != 0) return smith_untar_system(untar, "replace an earlier entry");
        fd = openat(dir, leaf, flags, permissions);
    }
    if (fd < 0) return smith_untar_system(untar, "create the file");
    untar->file = fd;
    untar->file_size = size;
    if (size == 0) return smith_untar_file_done(untar);
    untar->phase = SmithUntar_File;
    untar->remaining = size;
    return true;
}

constexpr uint64_t smith_untar_apple_double_min = 26; // the size of an AppleDouble header

// macOS tar writes a file's extended attributes as a companion entry "._name" in the AppleDouble format, and bsdtar
// turns it back into attributes instead of making a file. smith keeps no attributes, so such an entry is dropped; it is
// known by its name and by the format's magic number in its first bytes. A "._" file of anything else is kept.
static bool smith_untar_regular(SmithUntar *untar, uint64_t mode, uint64_t size) {
    const char *slash = strrchr(untar->name, '/');
    const char *base = slash != nullptr ? slash + 1 : untar->name;
    if (size < smith_untar_apple_double_min || strncmp(base, "._", 2) != 0)
        return smith_untar_create(untar, mode, size);
    untar->mode = mode;
    untar->file_size = size;
    untar->remaining = size;
    untar->sniffed = 0;
    untar->phase = SmithUntar_Sniff;
    return true;
}

// Takes the first four bytes of a "._" file, then drops the file if they are the AppleDouble magic number, else makes
// it and writes them. Returns the bytes taken, or 0 after setting the error.
static size_t smith_untar_sniff(SmithUntar *untar, const uint8_t *data, size_t size) {
    static const uint8_t magic[4] = {0x00, 0x05, 0x16, 0x07};
    size_t take = sizeof untar->sniff - untar->sniffed;
    if (take > size) take = size;
    memcpy(untar->sniff + untar->sniffed, data, take);
    untar->sniffed += take;
    untar->remaining -= take;
    if (untar->sniffed < sizeof untar->sniff) return take;
    if (memcmp(untar->sniff, magic, sizeof magic) == 0) {
        untar->remaining += (smith_untar_block - untar->file_size % smith_untar_block) % smith_untar_block;
        untar->phase = untar->remaining > 0 ? SmithUntar_Skip : SmithUntar_Header;
        return take;
    }
    if (!smith_untar_create(untar, untar->mode, untar->file_size)) return 0;
    untar->remaining -= sizeof untar->sniff;
    if (!smith_untar_write(untar->file, untar->sniff, sizeof untar->sniff)) {
        smith_untar_system(untar, "write the file");
        return 0;
    }
    return take;
}

static bool smith_untar_folder(SmithUntar *untar, uint64_t mode) {
    int dir = -1;
    const char *leaf = nullptr;
    if (!smith_untar_parent(untar, &dir, &leaf)) return false;
    if (mkdirat(dir, leaf, (mode_t)((mode & 0777U) | 0700U)) == 0) return true;
    struct stat info;
    if (errno != EEXIST) return smith_untar_system(untar, "make the folder");
    if (fstatat(dir, leaf, &info, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISDIR(info.st_mode)) {
        return smith_untar_reject(untar, "a folder replaces something that is not a folder");
    }
    return true;
}

// Frees the name `leaf` in `dir` for a link: a file or link already there is removed, a folder is an error.
static bool smith_untar_replace(SmithUntar *untar, int dir, const char *leaf) {
    if (unlinkat(dir, leaf, 0) != 0 && errno != ENOENT) return smith_untar_system(untar, "replace an earlier entry");
    return true;
}

static bool smith_untar_symlink(SmithUntar *untar, const char *target) {
    bool climbs = false;
    const char *reason = smith_untar_check_target(untar->name, target, &climbs);
    if (reason != nullptr) return smith_untar_reject(untar, reason);
    if (climbs && untar->climbing[0] == '\0') snprintf(untar->climbing, sizeof untar->climbing, "%s", untar->name);
    int dir = -1;
    const char *leaf = nullptr;
    if (!smith_untar_parent(untar, &dir, &leaf) || !smith_untar_replace(untar, dir, leaf)) return false;
    if (symlinkat(target, dir, leaf) != 0) return smith_untar_system(untar, "make the symlink");
    return true;
}

static bool smith_untar_hardlink(SmithUntar *untar, const char *raw) {
    char target[PATH_MAX];
    size_t components = 0;
    const char *reason = smith_untar_clean(raw, target, &components);
    if (reason == nullptr && components == 0) reason = "the hard link has no target";
    if (reason != nullptr) return smith_untar_reject(untar, reason);
    int dir = -1;
    const char *leaf = nullptr;
    if (!smith_untar_parent(untar, &dir, &leaf) || !smith_untar_replace(untar, dir, leaf)) return false;
    if (linkat(untar->root, target, dir, leaf, 0) != 0) return smith_untar_system(untar, "make the hard link");
    return true;
}

// A decimal number, all digits. False otherwise.
static bool smith_untar_decimal(const char *text, size_t length, uint64_t *value) {
    uint64_t result = 0;
    if (length == 0 || length > 19) return false;
    for (size_t i = 0; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        result = result * 10U + (uint64_t)(text[i] - '0');
    }
    *value = result;
    return true;
}

// A pax time, "seconds" or "seconds.fraction". Times before 1970 become 1970.
static bool smith_untar_pax_time(SmithUntar *untar, const char *value, size_t length) {
    uint64_t seconds = 0;
    size_t dot = 0;
    while (dot < length && value[dot] != '.')
        dot++;
    if (length > 0 && value[0] == '-') {
        untar->pax_seconds = 0;
        untar->pax_nanoseconds = 0;
        untar->has_mtime = true;
        return true;
    }
    if (!smith_untar_decimal(value, dot, &seconds)) return false;
    uint64_t nanoseconds = 0;
    for (size_t i = 0; i < 9; i++) {
        nanoseconds *= 10U;
        if (dot + 1 + i < length) {
            char digit = value[dot + 1 + i];
            if (digit < '0' || digit > '9') return false;
            nanoseconds += (uint64_t)(digit - '0');
        }
    }
    untar->pax_seconds = (int64_t)seconds;
    untar->pax_nanoseconds = (long)nanoseconds;
    untar->has_mtime = true;
    return true;
}

static bool smith_untar_copy_name(SmithUntar *untar, char *to, const char *from, size_t length) {
    if (length >= PATH_MAX)
        return smith_package_fail(untar->error, "tar: a long name in a header is over %d bytes", PATH_MAX);
    memcpy(to, from, length);
    to[length] = '\0';
    return true;
}

// One pax record's key and value.
static bool smith_untar_pax_record(SmithUntar *untar, const char *key, size_t key_length, const char *value,
                                   size_t value_length) {
    if (key_length == 4 && memcmp(key, "path", 4) == 0) {
        untar->has_path = true;
        return smith_untar_copy_name(untar, untar->path, value, value_length);
    }
    if (key_length == 8 && memcmp(key, "linkpath", 8) == 0) {
        untar->has_link = true;
        return smith_untar_copy_name(untar, untar->link, value, value_length);
    }
    if (key_length == 4 && memcmp(key, "size", 4) == 0) {
        untar->has_size = true;
        return smith_untar_decimal(value, value_length, &untar->size) ||
               smith_package_fail(untar->error, "tar: a pax size is damaged");
    }
    if (key_length == 5 && memcmp(key, "mtime", 5) == 0) {
        return smith_untar_pax_time(untar, value, value_length) ||
               smith_package_fail(untar->error, "tar: a pax time is damaged");
    }
    return true; // atime, uid, xattrs and the rest are not kept
}

// The records of a pax extended header: "<length> <key>=<value>\n", the length counting the whole record.
static bool smith_untar_pax(SmithUntar *untar) {
    const char *text = (const char *)untar->extension;
    size_t used = untar->extension_used;
    size_t at = 0;
    while (at < used) {
        uint64_t length = 0;
        size_t digits = 0;
        while (at + digits < used && text[at + digits] >= '0' && text[at + digits] <= '9')
            digits++;
        if (!smith_untar_decimal(text + at, digits, &length) || length < digits + 4 || length > used - at ||
            text[at + digits] != ' ' || text[at + length - 1] != '\n') {
            return smith_package_fail(untar->error, "tar: a pax header is damaged");
        }
        const char *key = text + at + digits + 1;
        const char *end = text + at + length - 1;
        const char *equals = memchr(key, '=', (size_t)(end - key));
        if (equals == nullptr) return smith_package_fail(untar->error, "tar: a pax header is damaged");
        if (!smith_untar_pax_record(untar, key, (size_t)(equals - key), equals + 1, (size_t)(end - equals - 1)))
            return false;
        at += length;
    }
    return true;
}

// The collected data of a pax or GNU long-name header is complete.
static bool smith_untar_extension_done(SmithUntar *untar) {
    const char *text = (const char *)untar->extension;
    size_t length = strnlen(text, untar->extension_used);
    if (untar->extension_kind == 'L') {
        untar->has_path = true;
        return smith_untar_copy_name(untar, untar->path, text, length);
    }
    if (untar->extension_kind == 'K') {
        untar->has_link = true;
        return smith_untar_copy_name(untar, untar->link, text, length);
    }
    return smith_untar_pax(untar);
}

static bool smith_untar_start_extension(SmithUntar *untar, char kind, uint64_t size) {
    if (size > smith_untar_extension_max) {
        return smith_package_fail(untar->error, "tar: an extended header of %llu bytes is over the limit of %zu",
                                  (unsigned long long)size, smith_untar_extension_max);
    }
    untar->extension_kind = kind;
    untar->extension_used = 0;
    untar->file_size = size;
    untar->remaining = size;
    untar->phase = SmithUntar_Extension;
    if (size > 0) return true;
    untar->phase = SmithUntar_Header;
    return smith_untar_extension_done(untar);
}

// The name in the header (ustar prefix and name, or just the name), unless a pax or GNU header overrode it.
static void smith_untar_raw_name(SmithUntar *untar, const uint8_t *header, char *raw) {
    if (untar->has_path) {
        snprintf(raw, PATH_MAX, "%s", untar->path);
        return;
    }
    size_t name = strnlen((const char *)header, 100);
    bool posix = memcmp(header + 257, "ustar", 5) == 0 && header[262] == 0;
    size_t prefix = posix ? strnlen((const char *)header + 345, 155) : 0;
    if (prefix > 0) {
        snprintf(raw, PATH_MAX, "%.*s/%.*s", (int)prefix, (const char *)header + 345, (int)name, (const char *)header);
    } else {
        snprintf(raw, PATH_MAX, "%.*s", (int)name, (const char *)header);
    }
}

static void smith_untar_clear_overrides(SmithUntar *untar) {
    untar->has_path = false;
    untar->has_link = false;
    untar->has_size = false;
    untar->has_mtime = false;
}

// Drops an entry's data and its padding.
static void smith_untar_skip(SmithUntar *untar, uint64_t size) {
    uint64_t padding = (smith_untar_block - size % smith_untar_block) % smith_untar_block;
    untar->remaining = size + padding;
    untar->phase = untar->remaining > 0 ? SmithUntar_Skip : SmithUntar_Header;
}

// What the entry makes, other than a regular file. Returns false after setting the error.
static bool smith_untar_make(SmithUntar *untar, char type, uint64_t mode, size_t components, const char *target) {
    if (components == 0) return type == '5' || smith_untar_reject(untar, "the entry has no name");
    if (type == '5') return smith_untar_folder(untar, mode);
    if (type == '2') return smith_untar_symlink(untar, target);
    if (type == '1') return smith_untar_hardlink(untar, target);
    return smith_untar_reject(untar, "the entry is a special file or a type smith does not extract");
}

// Starts the entry whose header is complete: its name, then what it makes.
static bool smith_untar_entry(SmithUntar *untar, char type, uint64_t size) {
    const uint8_t *header = untar->header;
    char raw[PATH_MAX];
    smith_untar_raw_name(untar, header, raw);
    size_t components = 0;
    const char *reason = smith_untar_clean(raw, untar->name, &components);
    if (reason != nullptr) {
        snprintf(untar->name, sizeof untar->name, "%.200s", raw);
        return smith_untar_reject(untar, reason);
    }
    uint64_t mode = 0;
    uint64_t seconds = 0;
    if (!smith_untar_number(header + 100, 8, &mode) || !smith_untar_number(header + 136, 12, &seconds)) {
        return smith_untar_reject(untar, "the header's mode or time is damaged");
    }
    untar->mtime_seconds = untar->has_mtime ? untar->pax_seconds : (int64_t)seconds;
    untar->mtime_nanoseconds = untar->has_mtime ? untar->pax_nanoseconds : 0;
    char link[PATH_MAX];
    snprintf(link, sizeof link, "%.*s", (int)strnlen((const char *)header + 157, 100), (const char *)header + 157);
    const char *target = untar->has_link ? untar->link : link;
    smith_untar_clear_overrides(untar);
    bool slash = raw[0] != '\0' && raw[strlen(raw) - 1] == '/'; // an old tar marks a folder by a trailing slash
    bool regular = type == '0' || type == '\0' || type == '7';
    if (regular && !slash && components > 0) return smith_untar_regular(untar, mode, size);
    if (regular && slash) type = '5';
    if (!smith_untar_make(untar, type, mode, components, target)) return false;
    smith_untar_skip(untar, size);
    return true;
}

// A complete 512-byte header.
static bool smith_untar_header(SmithUntar *untar) {
    const uint8_t *header = untar->header;
    if (smith_untar_is_zero(header, smith_untar_block)) {
        untar->zero_blocks++;
        if (untar->zero_blocks >= 2) untar->phase = SmithUntar_Ended;
        return true;
    }
    if (untar->zero_blocks != 0) return smith_package_fail(untar->error, "tar: a header follows a lone zero block");
    if (!smith_untar_header_checksum_ok(header)) {
        return smith_package_fail(untar->error,
                                  "tar: the header at offset %llu is damaged (its checksum does not match)",
                                  (unsigned long long)untar->header_offset);
    }
    uint64_t size = 0;
    if (!smith_untar_number(header + 124, 12, &size)) {
        return smith_package_fail(untar->error, "tar: the size in the header at offset %llu is damaged",
                                  (unsigned long long)untar->header_offset);
    }
    char type = (char)header[156];
    if (type == 'x' || type == 'L' || type == 'K') return smith_untar_start_extension(untar, type, size);
    if (type == 'g' || type == 'V') { // global pax headers and volume labels are not kept
        smith_untar_skip(untar, size);
        return true;
    }
    if (untar->has_size) size = untar->size;
    return smith_untar_entry(untar, type, size);
}

// Consumes some of `data` in the current phase and returns how much, or 0 after setting the error.
static size_t smith_untar_step(SmithUntar *untar, const uint8_t *data, size_t size) {
    size_t take = size;
    if (untar->phase == SmithUntar_Header) {
        if (untar->filled == 0) untar->header_offset = untar->offset;
        if (take > smith_untar_block - untar->filled) take = smith_untar_block - untar->filled;
        memcpy(untar->header + untar->filled, data, take);
        untar->filled += take;
        if (untar->filled < smith_untar_block) return take;
        untar->filled = 0;
        return smith_untar_header(untar) ? take : 0;
    }
    if (untar->phase == SmithUntar_Sniff) return smith_untar_sniff(untar, data, size);
    if (untar->phase == SmithUntar_Ended) {
        if (smith_untar_is_zero(data, size)) return size;
        smith_package_fail(untar->error, "tar: data follows the end of the archive");
        return 0;
    }
    if (take > untar->remaining) take = (size_t)untar->remaining;
    if (untar->phase == SmithUntar_File) {
        if (!smith_untar_write(untar->file, data, take)) {
            smith_untar_system(untar, "write the file");
            return 0;
        }
    } else if (untar->phase == SmithUntar_Extension) {
        memcpy(untar->extension + untar->extension_used, data, take);
        untar->extension_used += take;
    }
    untar->remaining -= take;
    if (untar->remaining > 0) return take;
    if (untar->phase == SmithUntar_File) return smith_untar_file_done(untar) ? take : 0;
    if (untar->phase == SmithUntar_Extension) {
        untar->phase = SmithUntar_Header;
        if (!smith_untar_extension_done(untar)) return 0;
        smith_untar_after_data(untar, untar->file_size);
        return take;
    }
    untar->phase = SmithUntar_Header;
    return take;
}

// The decompressor's sink: tar bytes, in pieces of any size.
static bool smith_untar_feed(void *context, const uint8_t *data, size_t size) {
    SmithUntar *untar = context;
    size_t passes = size; // every pass takes at least one byte
    while (size > 0) {
        SMITH_ASSERT(passes > 0);
        passes--;
        size_t used = smith_untar_step(untar, data, size);
        if (used == 0) return false;
        data += used;
        size -= used;
        untar->offset += used;
    }
    return true;
}

// The archive has ended: it must have ended where a tar does.
static bool smith_untar_finish(SmithUntar *untar) {
    if (untar->phase == SmithUntar_Ended) return true;
    if (untar->phase == SmithUntar_Header && untar->filled == 0 && untar->zero_blocks == 0) {
        return smith_package_fail(untar->error, "tar: the archive ends without its end marker (it is cut off)");
    }
    if (untar->phase == SmithUntar_File || untar->phase == SmithUntar_Sniff) {
        return smith_untar_reject(untar, "the archive ends inside this file (it is cut off)");
    }
    return smith_package_fail(untar->error, "tar: the archive ends inside an entry (it is cut off)");
}

static void smith_untar_close(SmithUntar *untar) {
    smith_untar_close_chain(untar, 0);
    if (untar->file >= 0) close(untar->file);
    if (untar->root >= 0) close(untar->root);
    untar->file = -1;
    untar->root = -1;
}

typedef enum {
    SmithArchive_Tar,
    SmithArchive_Gzip,
    SmithArchive_Xz,
    SmithArchive_Unknown,
} SmithArchiveFormat;

// The format by the first bytes. zip, bzip2 and zstd are recognised only to be refused by name.
static SmithArchiveFormat smith_archive_format(const uint8_t *data, size_t size, const char **refused) {
    *refused = nullptr;
    if (size >= 2 && data[0] == 0x1F && data[1] == 0x8B) return SmithArchive_Gzip;
    if (size >= 6 && memcmp(data,
                            "\xFD"
                            "7zXZ",
                            6) == 0)
        return SmithArchive_Xz;
    if (size >= 4 && memcmp(data, "PK\x03\x04", 4) == 0)
        *refused = "zip";
    else if (size >= 3 && memcmp(data, "BZh", 3) == 0)
        *refused = "bzip2";
    else if (size >= 4 && memcmp(data, "\x28\xB5\x2F\xFD", 4) == 0)
        *refused = "zstd";
    else if (size >= smith_untar_block && smith_untar_header_checksum_ok(data))
        return SmithArchive_Tar;
    return SmithArchive_Unknown;
}

typedef struct {
    char climbing[PATH_MAX]; // the first symlink that leaves its top-level folder, else ""
} SmithExtracted;

// A gzip's content is a tar, or else one file: the first block of what it holds says which. Until then the bytes
// wait here; a file is written to `name` in the extraction folder (mode 0644, never through a symlink).
typedef struct {
    SmithUntar *untar;
    const char *name;
    uint8_t probe[smith_untar_block];
    size_t filled;
    bool decided;
    int file; // the single file, or -1 when the content is a tar
} SmithGzipContent;

static bool smith_gzip_content_file(SmithGzipContent *content) {
    SmithUntar *untar = content->untar;
    int fd = openat(untar->root, content->name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) return smith_package_fail(untar->error, "cannot make %.200s: %s", content->name, strerror(errno));
    content->file = fd;
    return true;
}

static bool smith_gzip_content_decide(SmithGzipContent *content) {
    content->decided = true;
    bool tar = content->filled == smith_untar_block && smith_untar_header_checksum_ok(content->probe) &&
               !smith_untar_is_zero(content->probe, smith_untar_block);
    if (tar) return smith_untar_feed(content->untar, content->probe, content->filled);
    return smith_gzip_content_file(content) &&
           (content->filled == 0 || smith_untar_write(content->file, content->probe, content->filled) ||
            smith_package_fail(content->untar->error, "cannot write %.200s: %s", content->name, strerror(errno)));
}

static bool smith_gzip_content_feed(void *context, const uint8_t *data, size_t size) {
    SmithGzipContent *content = context;
    if (!content->decided) {
        size_t take = smith_untar_block - content->filled;
        if (take > size) take = size;
        memcpy(content->probe + content->filled, data, take);
        content->filled += take;
        data += take;
        size -= take;
        if (content->filled < smith_untar_block) return true;
        if (!smith_gzip_content_decide(content)) return false;
    }
    if (size == 0) return true;
    if (content->file < 0) return smith_untar_feed(content->untar, data, size);
    return smith_untar_write(content->file, data, size) ||
           smith_package_fail(content->untar->error, "cannot write %.200s: %s", content->name, strerror(errno));
}

static bool smith_gzip_content(SmithUntar *untar, const char *name, const uint8_t *data, size_t size,
                               SmithPackageFault *error) {
    SmithGzipContent content = {.untar = untar, .name = name, .file = -1};
    bool ok = smith_gzip_decode(data, size, smith_gzip_content_feed, &content, error);
    ok = ok && (content.decided || smith_gzip_content_decide(&content));
    if (content.file >= 0) {
        close(content.file);
        return ok;
    }
    return ok && smith_untar_finish(untar);
}

static bool smith_archive_decode(SmithUntar *untar, const char *name, const uint8_t *data, size_t size,
                                 SmithPackageFault *error) {
    const char *refused = nullptr;
    SmithArchiveFormat format = smith_archive_format(data, size, &refused);
    if (format == SmithArchive_Tar) return smith_untar_feed(untar, data, size) && smith_untar_finish(untar);
    if (format == SmithArchive_Gzip) return smith_gzip_content(untar, name, data, size, error);
    if (format == SmithArchive_Xz)
        return smith_xz_decode(data, size, smith_untar_feed, untar, error) && smith_untar_finish(untar);
    if (refused != nullptr)
        return smith_package_fail(error, "%s archives are not supported (tar, tar.gz, tar.xz and a single .gz are)",
                                  refused);
    return smith_package_fail(error, "not a tar, tar.gz, tar.xz or gzip archive");
}

// Extracts the archive file into the existing folder. `extracted` says what the caller must check afterwards. `name`
// is the file a gzip of one file (not a tar) becomes.
static bool smith_package_extract(const char *archive, const char *name, const char *folder, SmithExtracted *extracted,
                                  SmithPackageFault *error) {
    int fd = open(archive, O_RDONLY | O_CLOEXEC);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) != 0) {
        if (fd >= 0) close(fd);
        return smith_package_fail(error, "cannot read %s: %s", archive, strerror(errno));
    }
    if (info.st_size == 0) {
        close(fd);
        return smith_package_fail(error, "the archive is empty");
    }
    void *map = mmap(nullptr, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return smith_package_fail(error, "cannot map %s: %s", archive, strerror(errno));
    static SmithUntar untar;
    memset(&untar, 0, sizeof untar);
    untar.error = error;
    untar.file = -1;
    untar.root = open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    bool ok = untar.root >= 0 || smith_package_fail(error, "cannot open %s: %s", folder, strerror(errno));
    ok = ok && smith_archive_decode(&untar, name, map, (size_t)info.st_size, error);
    snprintf(extracted->climbing, sizeof extracted->climbing, "%s", untar.climbing);
    smith_untar_close(&untar);
    munmap(map, (size_t)info.st_size);
    return ok;
}
