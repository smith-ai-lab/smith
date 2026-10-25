// The handlers runtime (issue #239): one dylib that every smith program links, whose `__interpose` tuples send the
// allocator family to the allocator of the calling thread, so that code that calls malloc (smith's, a prebuilt
// library's, one loaded with dlopen) gets what the program set. No handler set is libc's own function. The functions
// call libc's by their names: a dylib's own calls are not interposed by its tuples (under ASan, linked before this
// dylib, they reach ASan's), so nothing here asks dlsym for them, which would itself allocate. The handler is found
// through a thread-local read only once `smith_handlers_ready` says the image's thread-local storage exists (dyld and
// ASan allocate before).
#include "smith/handlers.h"

#include <errno.h>
#include <fcntl.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static atomic_bool smith_handlers_ready;
static atomic_uint smith_handlers_active; // threads with an allocator set: 0 is the fast path, libc with no lookup
static thread_local SmithAllocator *smith_handlers_table;

__attribute__((constructor)) static void smith_handlers_start(void) {
    atomic_store(&smith_handlers_ready, true);
}

// The thread-local is read in a function of its own: unoptimized code computes a thread-local's address where the
// function starts, before the checks, and dyld calls close and write before the image's thread-local storage exists.
__attribute__((noinline)) static SmithAllocator *smith_handlers_table_get(void) {
    return smith_handlers_table;
}

__attribute__((noinline)) static SmithAllocator *smith_handlers_current(void) {
    if (atomic_load_explicit(&smith_handlers_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_table_get();
}

SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator) {
    SmithAllocator *previous = smith_handlers_table;
    smith_handlers_table = allocator;
    if (previous == nullptr && allocator != nullptr) atomic_fetch_add(&smith_handlers_active, 1);
    if (previous != nullptr && allocator == nullptr) atomic_fetch_sub(&smith_handlers_active, 1);
    return previous;
}

SmithAllocator *smith_handlers_allocator(void) {
    return smith_handlers_current();
}

// The files and the console, found the same way: a counter of the threads that have one, a thread-local table.
static atomic_uint smith_handlers_files_active;
static atomic_uint smith_handlers_console_active;
static thread_local SmithFiles *smith_handlers_files_table;
static thread_local SmithConsole *smith_handlers_console_table;

__attribute__((noinline)) static SmithFiles *smith_handlers_files_get(void) {
    return smith_handlers_files_table;
}

__attribute__((noinline)) static SmithConsole *smith_handlers_console_get(void) {
    return smith_handlers_console_table;
}

__attribute__((noinline)) static SmithFiles *smith_handlers_files_current(void) {
    if (atomic_load_explicit(&smith_handlers_files_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_files_get();
}

__attribute__((noinline)) static SmithConsole *smith_handlers_console_current(void) {
    if (atomic_load_explicit(&smith_handlers_console_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_console_get();
}

SmithFiles *smith_handlers_set_files(SmithFiles *files) {
    SmithFiles *previous = smith_handlers_files_table;
    smith_handlers_files_table = files;
    if (previous == nullptr && files != nullptr) atomic_fetch_add(&smith_handlers_files_active, 1);
    if (previous != nullptr && files == nullptr) atomic_fetch_sub(&smith_handlers_files_active, 1);
    return previous;
}

SmithFiles *smith_handlers_files(void) {
    return smith_handlers_files_current();
}

SmithConsole *smith_handlers_set_console(SmithConsole *console) {
    SmithConsole *previous = smith_handlers_console_table;
    smith_handlers_console_table = console;
    if (previous == nullptr && console != nullptr) atomic_fetch_add(&smith_handlers_console_active, 1);
    if (previous != nullptr && console == nullptr) atomic_fetch_sub(&smith_handlers_console_active, 1);
    return previous;
}

SmithConsole *smith_handlers_console(void) {
    return smith_handlers_console_current();
}

static bool smith_handlers_has(int flags, int bit) {
    return ((unsigned)flags & (unsigned)bit) != 0;
}

constexpr size_t smith_handlers_align = 16;

static bool smith_handlers_owns(SmithAllocator *allocator, const void *block) {
    return allocator != nullptr && block != nullptr && allocator->owns(allocator, block);
}

static void *smith_handlers_malloc(size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, smith_handlers_align) : malloc(size);
}

static void *smith_handlers_calloc(size_t count, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return calloc(count, size);
    size_t bytes = 0;
    if (__builtin_mul_overflow(count, size, &bytes)) {
        errno = ENOMEM;
        return nullptr;
    }
    void *block = allocator->alloc(allocator, bytes, smith_handlers_align);
    if (block != nullptr) memset(block, 0, bytes);
    return block;
}

static void *smith_handlers_realloc(void *block, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return realloc(block, size);
    if (block == nullptr) return allocator->alloc(allocator, size, smith_handlers_align);
    if (smith_handlers_owns(allocator, block)) return allocator->realloc(allocator, block, size, smith_handlers_align);
    return realloc(block, size);
}

static void smith_handlers_free(void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (smith_handlers_owns(allocator, block)) {
        allocator->free(allocator, block);
    } else {
        free(block);
    }
}

static int smith_handlers_posix_memalign(void **result, size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return posix_memalign(result, align, size);
    if (align < sizeof(void *) || (align & (align - 1)) != 0) return EINVAL;
    void *block = allocator->alloc(allocator, size, align);
    if (block == nullptr) return ENOMEM;
    *result = block;
    return 0;
}

static void *smith_handlers_aligned_alloc(size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return aligned_alloc(align, size);
    return allocator->alloc(allocator, size, align);
}

static void *smith_handlers_valloc(size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, 16384) : valloc(size);
}

static size_t smith_handlers_malloc_size(const void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (!smith_handlers_owns(allocator, block)) return malloc_size(block);
    return allocator->size != nullptr ? allocator->size(allocator, block) : 0;
}

// The zone functions are one passthrough: a block of the current allocator answers whatever zone is named, anything
// else is the zone's own (spike 3 of #208: CoreFoundation and Objective-C ask the zone about a pointer, and free
// through it).
static void *smith_handlers_zone_malloc(malloc_zone_t *zone, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, smith_handlers_align)
                                : malloc_zone_malloc(zone, size);
}

static void *smith_handlers_zone_calloc(malloc_zone_t *zone, size_t count, size_t size) {
    return smith_handlers_current() != nullptr ? smith_handlers_calloc(count, size)
                                               : malloc_zone_calloc(zone, count, size);
}

static void *smith_handlers_zone_valloc(malloc_zone_t *zone, size_t size) {
    return smith_handlers_current() != nullptr ? smith_handlers_valloc(size) : malloc_zone_valloc(zone, size);
}

static void *smith_handlers_zone_memalign(malloc_zone_t *zone, size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, align) : malloc_zone_memalign(zone, align, size);
}

static void *smith_handlers_zone_realloc(malloc_zone_t *zone, void *block, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return malloc_zone_realloc(zone, block, size);
    if (block == nullptr || smith_handlers_owns(allocator, block)) return smith_handlers_realloc(block, size);
    return malloc_zone_realloc(zone, block, size);
}

static void smith_handlers_zone_free(malloc_zone_t *zone, void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (smith_handlers_owns(allocator, block)) {
        allocator->free(allocator, block);
    } else {
        malloc_zone_free(zone, block);
    }
}

static malloc_zone_t *smith_handlers_zone_from_ptr(const void *block) {
    return smith_handlers_owns(smith_handlers_current(), block) ? malloc_default_zone() : malloc_zone_from_ptr(block);
}

// ---- The file descriptors: 0 is the console's input, 1 and 2 its output, 3 and up the files'.

constexpr int smith_handlers_first_file = 3;

static int smith_handlers_open(const char *path, int flags, ...) {
    int mode = 0;
    if (smith_handlers_has(flags, O_CREAT)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    SmithFiles *files = smith_handlers_files_current();
    return files != nullptr ? files->open(files, path, flags, mode) : open(path, flags, mode);
}

static ssize_t smith_handlers_read(int fd, void *buffer, size_t size) {
    SmithConsole *console = fd == 0 ? smith_handlers_console_current() : nullptr;
    if (console != nullptr) return console->read(console, buffer, size);
    SmithFiles *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    return files != nullptr ? files->read(files, fd, buffer, size) : read(fd, buffer, size);
}

static ssize_t smith_handlers_write(int fd, const void *buffer, size_t size) {
    SmithConsole *console = fd == 1 || fd == 2 ? smith_handlers_console_current() : nullptr;
    if (console != nullptr) return console->write(console, fd, buffer, size);
    SmithFiles *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    return files != nullptr ? files->write(files, fd, buffer, size) : write(fd, buffer, size);
}

static int smith_handlers_close(int fd) {
    SmithFiles *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    return files != nullptr ? files->close(files, fd) : close(fd);
}

// syscall(SYS_read, ...) and syscall(SYS_write, ...) are read and write; every other number is the kernel's.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static int smith_handlers_syscall(int number, ...) {
    long argument[6] = {};
    va_list args;
    va_start(args, number);
    for (size_t i = 0; i < 6; i++)
        argument[i] = va_arg(args, long);
    va_end(args);
    if (number == SYS_read) return (int)smith_handlers_read((int)argument[0], (void *)argument[1], (size_t)argument[2]);
    if (number == SYS_write)
        return (int)smith_handlers_write((int)argument[0], (const void *)argument[1], (size_t)argument[2]);
    return syscall(number, argument[0], argument[1], argument[2], argument[3], argument[4], argument[5]);
}
#pragma clang diagnostic pop

// ---- stdio. The C library writes stdout and reads stdin through entry points no tuple reaches, so while a console is
// set the standard streams are other streams, made with funopen over the console's functions, and each stdio function
// that takes a stream (or means stdout or stdin) is called on the stand-in. A stream is looked at through the console
// of the thread that uses it, so the stand-ins are shared.

static FILE *smith_handlers_console_streams[3]; // stdin, stdout, stderr
static pthread_once_t smith_handlers_console_once = PTHREAD_ONCE_INIT;

static int smith_handlers_console_write(void *cookie, const char *data, int size) {
    int fd = (int)(intptr_t)cookie;
    SmithConsole *console = smith_handlers_console_current();
    return (int)(console != nullptr ? console->write(console, fd, data, (size_t)size) : write(fd, data, (size_t)size));
}

static int smith_handlers_console_read(void *cookie, char *data, int size) {
    (void)cookie;
    SmithConsole *console = smith_handlers_console_current();
    return (int)(console != nullptr ? console->read(console, data, (size_t)size) : read(0, data, (size_t)size));
}

static void smith_handlers_console_make(void) {
    smith_handlers_console_streams[0] = funopen(nullptr, smith_handlers_console_read, nullptr, nullptr, nullptr);
    for (int fd = 1; fd <= 2; fd++) {
        FILE *out = funopen((const void *)(intptr_t)fd, nullptr, smith_handlers_console_write, nullptr, nullptr);
        if (out != nullptr) setvbuf(out, nullptr, _IONBF, 0); // what is written reaches the handler at once, in order
        smith_handlers_console_streams[fd] = out;
    }
}

// The stream a stdio function should use for `stream`: a stand-in for stdin, stdout or stderr while a console is set.
static FILE *smith_handlers_map(FILE *stream) {
    if (smith_handlers_console_current() == nullptr) return stream;
    int index = stream == stdin ? 0 : stream == stdout ? 1 : stream == stderr ? 2 : -1;
    if (index < 0) return stream;
    pthread_once(&smith_handlers_console_once, smith_handlers_console_make);
    return smith_handlers_console_streams[index] != nullptr ? smith_handlers_console_streams[index] : stream;
}

static size_t smith_handlers_fwrite(const void *data, size_t size, size_t count, FILE *stream) {
    return fwrite(data, size, count, smith_handlers_map(stream));
}

static size_t smith_handlers_fread(void *data, size_t size, size_t count, FILE *stream) {
    return fread(data, size, count, smith_handlers_map(stream));
}

static char *smith_handlers_fgets(char *line, int size, FILE *stream) {
    return fgets(line, size, smith_handlers_map(stream));
}

static int smith_handlers_fgetc(FILE *stream) {
    return fgetc(smith_handlers_map(stream));
}

static int smith_handlers_getchar(void) {
    return fgetc(smith_handlers_map(stdin));
}

static int smith_handlers_ungetc(int c, FILE *stream) {
    return ungetc(c, smith_handlers_map(stream));
}

static int smith_handlers_fputs(const char *text, FILE *stream) {
    return fputs(text, smith_handlers_map(stream));
}

static int smith_handlers_fputc(int c, FILE *stream) {
    return fputc(c, smith_handlers_map(stream));
}

static int smith_handlers_putchar(int c) {
    return fputc(c, smith_handlers_map(stdout));
}

static int smith_handlers_puts(const char *text) {
    FILE *out = smith_handlers_map(stdout);
    return fputs(text, out) < 0 || fputc('\n', out) < 0 ? EOF : 1;
}

[[gnu::format(printf, 2, 0)]] static int smith_handlers_vfprintf(FILE *stream, const char *format, va_list args) {
    return vfprintf(smith_handlers_map(stream), format, args);
}

[[gnu::format(printf, 2, 3)]] static int smith_handlers_fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = smith_handlers_vfprintf(stream, format, args);
    va_end(args);
    return written;
}

[[gnu::format(printf, 1, 0)]] static int smith_handlers_vprintf(const char *format, va_list args) {
    return smith_handlers_vfprintf(stdout, format, args);
}

[[gnu::format(printf, 1, 2)]] static int smith_handlers_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = smith_handlers_vfprintf(stdout, format, args);
    va_end(args);
    return written;
}

[[gnu::format(printf, 2, 0)]] static int smith_handlers_vdprintf(int fd, const char *format, va_list args) {
    bool console = (fd == 1 || fd == 2) && smith_handlers_console_current() != nullptr;
    return console ? smith_handlers_vfprintf(fd == 1 ? stdout : stderr, format, args) : vdprintf(fd, format, args);
}

[[gnu::format(printf, 2, 3)]] static int smith_handlers_dprintf(int fd, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = smith_handlers_vdprintf(fd, format, args);
    va_end(args);
    return written;
}

static int smith_handlers_fflush(FILE *stream) {
    return fflush(stream == nullptr ? nullptr : smith_handlers_map(stream));
}

static int smith_handlers_fscanf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int matched = vfscanf(smith_handlers_map(stream), format, args);
    va_end(args);
    return matched;
}

static int smith_handlers_scanf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int matched = vfscanf(smith_handlers_map(stdin), format, args);
    va_end(args);
    return matched;
}

static ssize_t smith_handlers_getline(char **line, size_t *capacity, FILE *stream) {
    return getline(line, capacity, smith_handlers_map(stream));
}

static int smith_handlers_fseek(FILE *stream, long offset, int whence) {
    return fseek(smith_handlers_map(stream), offset, whence);
}

static int smith_handlers_fclose(FILE *stream) {
    return fclose(stream);
}

// ---- Files through stdio: while a files handler is set, fopen, fdopen and freopen return a funopen stream whose
// callbacks call the handler's read, write and close (a handler has no seek, so such a stream cannot be repositioned).
// The stream keeps the handler that opened it: close it before the handler goes away.

typedef struct {
    SmithFiles *files;
    int fd;
} SmithHandlersFile;

static int smith_handlers_file_read(void *cookie, char *data, int size) {
    const SmithHandlersFile *file = cookie;
    return (int)file->files->read(file->files, file->fd, data, (size_t)size);
}

static int smith_handlers_file_write(void *cookie, const char *data, int size) {
    const SmithHandlersFile *file = cookie;
    return (int)file->files->write(file->files, file->fd, data, (size_t)size);
}

static int smith_handlers_file_close(void *cookie) {
    SmithHandlersFile *file = cookie;
    int status = file->files->close(file->files, file->fd);
    free(file);
    return status;
}

// The open flags of an fopen mode: "r", "w", "a", each with "+" and "b".
static int smith_handlers_mode_flags(const char *mode) {
    bool plus = strchr(mode, '+') != nullptr;
    unsigned access = (unsigned)(plus ? O_RDWR : O_WRONLY);
    if (mode[0] == 'w') return (int)(access | (unsigned)O_CREAT | (unsigned)O_TRUNC);
    if (mode[0] == 'a') return (int)(access | (unsigned)O_CREAT | (unsigned)O_APPEND);
    return plus ? O_RDWR : O_RDONLY;
}

static FILE *smith_handlers_stream(SmithFiles *files, int fd, const char *mode) {
    SmithHandlersFile *file = malloc(sizeof *file);
    if (file == nullptr) return nullptr;
    *file = (SmithHandlersFile){files, fd};
    bool readable = mode[0] == 'r' || strchr(mode, '+') != nullptr;
    bool writable = mode[0] != 'r' || strchr(mode, '+') != nullptr;
    FILE *stream = funopen(file, readable ? smith_handlers_file_read : nullptr,
                           writable ? smith_handlers_file_write : nullptr, nullptr, smith_handlers_file_close);
    if (stream == nullptr) free(file);
    return stream;
}

static FILE *smith_handlers_fopen(const char *path, const char *mode) {
    SmithFiles *files = smith_handlers_files_current();
    if (files == nullptr) return fopen(path, mode);
    int fd = files->open(files, path, smith_handlers_mode_flags(mode), 0666);
    FILE *stream = fd >= 0 ? smith_handlers_stream(files, fd, mode) : nullptr;
    if (fd >= 0 && stream == nullptr) files->close(files, fd);
    return stream;
}

static FILE *smith_handlers_fdopen(int fd, const char *mode) {
    SmithFiles *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    return files != nullptr ? smith_handlers_stream(files, fd, mode) : fdopen(fd, mode);
}

// With a files handler set the stream is a new one (a funopen stream cannot take the place of the old FILE): the old
// stream is closed unless it is a standard one, and the result is the new stream.
static FILE *smith_handlers_freopen(const char *path, const char *mode, FILE *stream) {
    if (smith_handlers_files_current() == nullptr || path == nullptr) return freopen(path, mode, stream);
    if (stream != stdin && stream != stdout && stream != stderr) fclose(stream);
    return smith_handlers_fopen(path, mode);
}

typedef struct {
    uintptr_t replacement;
    uintptr_t replacee;
} SmithInterpose;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // syscall
// clang-format off
__attribute__((used, section("__DATA,__interpose"))) static const SmithInterpose smith_handlers_tuples[] = { // NOLINT(smith-constexpr): addresses are not constant expressions
    {(uintptr_t)&smith_handlers_malloc, (uintptr_t)&malloc},
    {(uintptr_t)&smith_handlers_calloc, (uintptr_t)&calloc},
    {(uintptr_t)&smith_handlers_realloc, (uintptr_t)&realloc},
    {(uintptr_t)&smith_handlers_free, (uintptr_t)&free},
    {(uintptr_t)&smith_handlers_posix_memalign, (uintptr_t)&posix_memalign},
    {(uintptr_t)&smith_handlers_aligned_alloc, (uintptr_t)&aligned_alloc},
    {(uintptr_t)&smith_handlers_valloc, (uintptr_t)&valloc},
    {(uintptr_t)&smith_handlers_malloc_size, (uintptr_t)&malloc_size},
    {(uintptr_t)&smith_handlers_zone_malloc, (uintptr_t)&malloc_zone_malloc},
    {(uintptr_t)&smith_handlers_zone_calloc, (uintptr_t)&malloc_zone_calloc},
    {(uintptr_t)&smith_handlers_zone_valloc, (uintptr_t)&malloc_zone_valloc},
    {(uintptr_t)&smith_handlers_zone_memalign, (uintptr_t)&malloc_zone_memalign},
    {(uintptr_t)&smith_handlers_zone_realloc, (uintptr_t)&malloc_zone_realloc},
    {(uintptr_t)&smith_handlers_zone_free, (uintptr_t)&malloc_zone_free},
    {(uintptr_t)&smith_handlers_zone_from_ptr, (uintptr_t)&malloc_zone_from_ptr},
    {(uintptr_t)&smith_handlers_open, (uintptr_t)&open},
    {(uintptr_t)&smith_handlers_read, (uintptr_t)&read},
    {(uintptr_t)&smith_handlers_write, (uintptr_t)&write},
    {(uintptr_t)&smith_handlers_close, (uintptr_t)&close},
    {(uintptr_t)&smith_handlers_syscall, (uintptr_t)&syscall},
    {(uintptr_t)&smith_handlers_fwrite, (uintptr_t)&fwrite},
    {(uintptr_t)&smith_handlers_fread, (uintptr_t)&fread},
    {(uintptr_t)&smith_handlers_fgets, (uintptr_t)&fgets},
    {(uintptr_t)&smith_handlers_fgetc, (uintptr_t)&fgetc},
    {(uintptr_t)&smith_handlers_fgetc, (uintptr_t)&getc},
    {(uintptr_t)&smith_handlers_getchar, (uintptr_t)&getchar},
    {(uintptr_t)&smith_handlers_ungetc, (uintptr_t)&ungetc},
    {(uintptr_t)&smith_handlers_fputs, (uintptr_t)&fputs},
    {(uintptr_t)&smith_handlers_fputc, (uintptr_t)&fputc},
    {(uintptr_t)&smith_handlers_fputc, (uintptr_t)&putc},
    {(uintptr_t)&smith_handlers_putchar, (uintptr_t)&putchar},
    {(uintptr_t)&smith_handlers_puts, (uintptr_t)&puts},
    {(uintptr_t)&smith_handlers_fprintf, (uintptr_t)&fprintf},
    {(uintptr_t)&smith_handlers_vfprintf, (uintptr_t)&vfprintf},
    {(uintptr_t)&smith_handlers_printf, (uintptr_t)&printf},
    {(uintptr_t)&smith_handlers_vprintf, (uintptr_t)&vprintf},
    {(uintptr_t)&smith_handlers_dprintf, (uintptr_t)&dprintf},
    {(uintptr_t)&smith_handlers_vdprintf, (uintptr_t)&vdprintf},
    {(uintptr_t)&smith_handlers_fflush, (uintptr_t)&fflush},
    {(uintptr_t)&smith_handlers_fopen, (uintptr_t)&fopen},
    {(uintptr_t)&smith_handlers_fclose, (uintptr_t)&fclose},
    {(uintptr_t)&smith_handlers_fscanf, (uintptr_t)&fscanf},
    {(uintptr_t)&smith_handlers_scanf, (uintptr_t)&scanf},
    {(uintptr_t)&smith_handlers_getline, (uintptr_t)&getline},
    {(uintptr_t)&smith_handlers_fseek, (uintptr_t)&fseek},
    {(uintptr_t)&smith_handlers_fdopen, (uintptr_t)&fdopen},
    {(uintptr_t)&smith_handlers_freopen, (uintptr_t)&freopen},
};
// clang-format on
#pragma clang diagnostic pop
