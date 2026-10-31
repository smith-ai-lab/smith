// The handlers runtime (issue #239): one dylib that every smith program links, whose `__interpose` tuples send the
// allocator family to the allocator of the calling thread, so that code that calls malloc (smith's, a prebuilt
// library's, one loaded with dlopen) gets what the program set. No handler set is libc's own function. The functions
// call libc's by their names: a dylib's own calls are not interposed by its tuples (under ASan, linked before this
// dylib, they reach ASan's), so nothing here asks dlsym for them, which would itself allocate. The handler is found
// through a thread-local read only once `smith_handlers_ready` says the image's thread-local storage exists (dyld and
// ASan allocate before).
#ifdef __linux__
// What glibc hides from C23 and macOS does not: fopencookie, RTLD_NEXT, O_TMPFILE (knowledge/platform-layer.md).
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming): glibc's name for it
#define _GNU_SOURCE
#endif
#include "smith/handlers.h"

#include <errno.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <dlfcn.h>
#endif
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static atomic_bool smith_handlers_ready;
static atomic_uint smith_handlers_active; // threads with an allocator set: 0 is the fast path, libc with no lookup
static atomic_uint smith_handlers_files_active;
static atomic_uint smith_handlers_console_active;
static atomic_uint smith_handlers_env_active;
static atomic_uint smith_handlers_clock_active;
static atomic_uint smith_handlers_entropy_active;
static atomic_uint smith_handlers_net_active;
static atomic_uint smith_handlers_process_active;
static atomic_uint smith_handlers_scheduler_active;

// The handlers of a thread are one table (SmithHandlers). A thread has a table of its own, and a thread-local pointer
// names the current one (null: its own); the task runtime swaps the pointer with smith_handlers_set_table. The counters
// above count the non-null handlers of the tables that are current, so a thread that sets none costs one load.
static thread_local SmithHandlers smith_handlers_own;
static thread_local SmithHandlers *smith_handlers_current_table;

// Inlined into the noinline functions that read it: the thread-local is still read only after the counter and ready
// checks.
__attribute__((always_inline)) static inline SmithHandlers *smith_handlers_tab(void) {
    return smith_handlers_current_table != nullptr ? smith_handlers_current_table : &smith_handlers_own;
}

static void smith_handlers_count(atomic_uint *counter, const void *from, const void *to) {
    if (from == nullptr && to != nullptr) atomic_fetch_add(counter, 1);
    if (from != nullptr && to == nullptr) atomic_fetch_sub(counter, 1);
}

static void smith_handlers_recount(const SmithHandlers *from, const SmithHandlers *to) {
    smith_handlers_count(&smith_handlers_active, from->allocator, to->allocator);
    smith_handlers_count(&smith_handlers_files_active, from->files, to->files);
    smith_handlers_count(&smith_handlers_console_active, from->console, to->console);
    smith_handlers_count(&smith_handlers_env_active, from->env, to->env);
    smith_handlers_count(&smith_handlers_clock_active, from->clock, to->clock);
    smith_handlers_count(&smith_handlers_entropy_active, from->entropy, to->entropy);
    smith_handlers_count(&smith_handlers_net_active, from->net, to->net);
    smith_handlers_count(&smith_handlers_process_active, from->process, to->process);
    smith_handlers_count(&smith_handlers_scheduler_active, from->scheduler, to->scheduler);
}

SmithHandlers *smith_handlers_table(void) {
    return smith_handlers_tab();
}

SmithHandlers *smith_handlers_set_table(SmithHandlers *table) {
    SmithHandlers *previous = smith_handlers_tab();
    SmithHandlers *next = table != nullptr ? table : &smith_handlers_own;
    smith_handlers_recount(previous, next);
    smith_handlers_current_table = next;
    return previous;
}

__attribute__((constructor)) static void smith_handlers_start(void) {
    atomic_store(&smith_handlers_ready, true);
}

// The thread-local is read in a function of its own: unoptimized code computes a thread-local's address where the
// function starts, before the checks, and dyld calls close and write before the image's thread-local storage exists.
__attribute__((noinline)) static SmithAllocator *smith_handlers_table_get(void) {
    return smith_handlers_tab()->allocator;
}

__attribute__((noinline)) static SmithAllocator *smith_handlers_current(void) {
    if (atomic_load_explicit(&smith_handlers_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_table_get();
}

SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator) {
    SmithAllocator *previous = smith_handlers_tab()->allocator;
    smith_handlers_tab()->allocator = allocator;
    if (previous == nullptr && allocator != nullptr) atomic_fetch_add(&smith_handlers_active, 1);
    if (previous != nullptr && allocator == nullptr) atomic_fetch_sub(&smith_handlers_active, 1);
    return previous;
}

SmithAllocator *smith_handlers_allocator(void) {
    return smith_handlers_current();
}

// The files and the console, found the same way: a counter of the threads that have one, a thread-local table.

__attribute__((noinline)) static SmithFileHandler *smith_handlers_files_get(void) {
    return smith_handlers_tab()->files;
}

__attribute__((noinline)) static SmithConsoleHandler *smith_handlers_console_get(void) {
    return smith_handlers_tab()->console;
}

__attribute__((noinline)) static SmithFileHandler *smith_handlers_files_current(void) {
    if (atomic_load_explicit(&smith_handlers_files_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_files_get();
}

__attribute__((noinline)) static SmithConsoleHandler *smith_handlers_console_current(void) {
    if (atomic_load_explicit(&smith_handlers_console_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_console_get();
}

SmithFileHandler *smith_handlers_set_files(SmithFileHandler *files) {
    SmithFileHandler *previous = smith_handlers_tab()->files;
    smith_handlers_tab()->files = files;
    if (previous == nullptr && files != nullptr) atomic_fetch_add(&smith_handlers_files_active, 1);
    if (previous != nullptr && files == nullptr) atomic_fetch_sub(&smith_handlers_files_active, 1);
    return previous;
}

SmithFileHandler *smith_handlers_files(void) {
    return smith_handlers_files_current();
}

SmithConsoleHandler *smith_handlers_set_console(SmithConsoleHandler *console) {
    SmithConsoleHandler *previous = smith_handlers_tab()->console;
    smith_handlers_tab()->console = console;
    if (previous == nullptr && console != nullptr) atomic_fetch_add(&smith_handlers_console_active, 1);
    if (previous != nullptr && console == nullptr) atomic_fetch_sub(&smith_handlers_console_active, 1);
    return previous;
}

SmithConsoleHandler *smith_handlers_console(void) {
    return smith_handlers_console_current();
}

// The environment and the clock, found the same way.

__attribute__((noinline)) static SmithEnvHandler *smith_handlers_env_get(void) {
    return smith_handlers_tab()->env;
}

__attribute__((noinline)) static SmithClockHandler *smith_handlers_clock_get(void) {
    return smith_handlers_tab()->clock;
}

__attribute__((noinline)) static SmithEnvHandler *smith_handlers_env_current(void) {
    if (atomic_load_explicit(&smith_handlers_env_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_env_get();
}

__attribute__((noinline)) static SmithClockHandler *smith_handlers_clock_current(void) {
    if (atomic_load_explicit(&smith_handlers_clock_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_clock_get();
}

SmithEnvHandler *smith_handlers_set_env(SmithEnvHandler *env) {
    SmithEnvHandler *previous = smith_handlers_tab()->env;
    smith_handlers_tab()->env = env;
    if (previous == nullptr && env != nullptr) atomic_fetch_add(&smith_handlers_env_active, 1);
    if (previous != nullptr && env == nullptr) atomic_fetch_sub(&smith_handlers_env_active, 1);
    return previous;
}

SmithEnvHandler *smith_handlers_env(void) {
    return smith_handlers_env_current();
}

SmithClockHandler *smith_handlers_set_clock(SmithClockHandler *clock) {
    SmithClockHandler *previous = smith_handlers_tab()->clock;
    smith_handlers_tab()->clock = clock;
    if (previous == nullptr && clock != nullptr) atomic_fetch_add(&smith_handlers_clock_active, 1);
    if (previous != nullptr && clock == nullptr) atomic_fetch_sub(&smith_handlers_clock_active, 1);
    return previous;
}

SmithClockHandler *smith_handlers_clock(void) {
    return smith_handlers_clock_current();
}

// The entropy, found the same way.

__attribute__((noinline)) static SmithEntropyHandler *smith_handlers_entropy_get(void) {
    return smith_handlers_tab()->entropy;
}

__attribute__((noinline)) static SmithEntropyHandler *smith_handlers_entropy_current(void) {
    if (atomic_load_explicit(&smith_handlers_entropy_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_entropy_get();
}

SmithEntropyHandler *smith_handlers_set_entropy(SmithEntropyHandler *entropy) {
    SmithEntropyHandler *previous = smith_handlers_tab()->entropy;
    smith_handlers_tab()->entropy = entropy;
    if (previous == nullptr && entropy != nullptr) atomic_fetch_add(&smith_handlers_entropy_active, 1);
    if (previous != nullptr && entropy == nullptr) atomic_fetch_sub(&smith_handlers_entropy_active, 1);
    return previous;
}

SmithEntropyHandler *smith_handlers_entropy(void) {
    return smith_handlers_entropy_current();
}

// A timespec as nanoseconds, and nanoseconds as a timespec; a duration too long for 64 bits is the longest one.
static uint64_t smith_handlers_nanoseconds(const struct timespec *duration) {
    constexpr uint64_t second = 1'000'000'000;
    if (duration->tv_sec < 0) return 0;
    if ((uint64_t)duration->tv_sec > UINT64_MAX / second - 1) return UINT64_MAX;
    return (uint64_t)duration->tv_sec * second + (uint64_t)duration->tv_nsec;
}

static int smith_handlers_clock_read(SmithClockHandler *clock, clockid_t which, struct timespec *time) {
    constexpr int64_t second = 1'000'000'000;
    int64_t nanoseconds = clock->now(clock, (int)which);
    *time = (struct timespec){.tv_sec = (time_t)(nanoseconds / second), .tv_nsec = (long)(nanoseconds % second)};
    return 0;
}

static int smith_handlers_clock_sleep(SmithClockHandler *clock, uint64_t nanoseconds) {
    clock->sleep(clock, nanoseconds);
    return 0;
}

// The network, found the same way (no libc function is interposed: smith::net calls the handler).

__attribute__((noinline)) static SmithNetHandler *smith_handlers_net_get(void) {
    return smith_handlers_tab()->net;
}

__attribute__((noinline)) static SmithNetHandler *smith_handlers_net_current(void) {
    if (atomic_load_explicit(&smith_handlers_net_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_net_get();
}

SmithNetHandler *smith_handlers_set_net(SmithNetHandler *net) {
    SmithNetHandler *previous = smith_handlers_tab()->net;
    smith_handlers_tab()->net = net;
    if (previous == nullptr && net != nullptr) atomic_fetch_add(&smith_handlers_net_active, 1);
    if (previous != nullptr && net == nullptr) atomic_fetch_sub(&smith_handlers_net_active, 1);
    return previous;
}

SmithNetHandler *smith_handlers_net(void) {
    return smith_handlers_net_current();
}

// The child processes, found the same way (no libc function is interposed: smith::process calls the handler).

__attribute__((noinline)) static SmithProcessHandler *smith_handlers_process_get(void) {
    return smith_handlers_tab()->process;
}

__attribute__((noinline)) static SmithProcessHandler *smith_handlers_process_current(void) {
    if (atomic_load_explicit(&smith_handlers_process_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_process_get();
}

SmithProcessHandler *smith_handlers_set_process(SmithProcessHandler *process) {
    SmithProcessHandler *previous = smith_handlers_tab()->process;
    smith_handlers_tab()->process = process;
    if (previous == nullptr && process != nullptr) atomic_fetch_add(&smith_handlers_process_active, 1);
    if (previous != nullptr && process == nullptr) atomic_fetch_sub(&smith_handlers_process_active, 1);
    return previous;
}

SmithProcessHandler *smith_handlers_process(void) {
    return smith_handlers_process_current();
}

// The scheduler, found the same way. The calls it answers (read, write, pread, pwrite, readv, writev, send, recv,
// accept, connect) build a request and hand it to `io`; with no scheduler, or one without `io`, they are the real
// calls.

__attribute__((noinline)) static SmithScheduler *smith_handlers_scheduler_get(void) {
    return smith_handlers_tab()->scheduler;
}

__attribute__((noinline)) static SmithScheduler *smith_handlers_scheduler_current(void) {
    if (atomic_load_explicit(&smith_handlers_scheduler_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_scheduler_get();
}

SmithScheduler *smith_handlers_set_scheduler(SmithScheduler *scheduler) {
    SmithScheduler *previous = smith_handlers_tab()->scheduler;
    smith_handlers_tab()->scheduler = scheduler;
    if (previous == nullptr && scheduler != nullptr) atomic_fetch_add(&smith_handlers_scheduler_active, 1);
    if (previous != nullptr && scheduler == nullptr) atomic_fetch_sub(&smith_handlers_scheduler_active, 1);
    return previous;
}

SmithScheduler *smith_handlers_scheduler(void) {
    return smith_handlers_scheduler_current();
}

// The pthread mutexes the calling thread holds (issue #938), counted only while some scheduler is set. A task never
// switches while it holds one, so per thread is per task. A routed call made above zero is the real call: parking there
// could leave another task of the same worker blocked on that mutex for good.
static thread_local int smith_handlers_locks_held;

static void smith_handlers_lock_taken(void) {
    if (atomic_load_explicit(&smith_handlers_scheduler_active, memory_order_relaxed) != 0) smith_handlers_locks_held += 1;
}

static void smith_handlers_lock_given(void) {
    if (smith_handlers_locks_held > 0) smith_handlers_locks_held -= 1;
}

// True when a scheduler took the request: its result is then the call's, with errno set as the real call would set it.
// The counter is read here, so a thread with no scheduler pays one load.
static bool smith_handlers_route(SmithIoRequest *request) {
    if (atomic_load_explicit(&smith_handlers_scheduler_active, memory_order_relaxed) == 0) return false;
    if (smith_handlers_locks_held > 0) return false;
    SmithScheduler *scheduler = smith_handlers_scheduler_current();
    if (scheduler == nullptr || scheduler->io == nullptr) return false;
    scheduler->io(scheduler, request);
    if (request->result < 0) errno = request->error;
    return true;
}

static bool smith_handlers_has(int flags, int bit) {
    return ((unsigned)flags & (unsigned)bit) != 0;
}

constexpr size_t smith_handlers_align = 16;

static bool smith_handlers_owns(SmithAllocator *allocator, const void *block) {
    return allocator != nullptr && block != nullptr && allocator->owns(allocator, block);
}

#ifdef __APPLE__
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
    SmithFileHandler *files = smith_handlers_files_current();
    return files != nullptr ? files->open(files, path, flags, mode) : open(path, flags, mode);
}

static int smith_handlers_mutex_lock(pthread_mutex_t *mutex) {
    int result = pthread_mutex_lock(mutex);
    if (result == 0) smith_handlers_lock_taken();
    return result;
}

static int smith_handlers_mutex_trylock(pthread_mutex_t *mutex) {
    int result = pthread_mutex_trylock(mutex);
    if (result == 0) smith_handlers_lock_taken();
    return result;
}

static int smith_handlers_mutex_unlock(pthread_mutex_t *mutex) {
    smith_handlers_lock_given();
    return pthread_mutex_unlock(mutex);
}

static ssize_t smith_handlers_read(int fd, void *buffer, size_t size) {
    SmithConsoleHandler *console = fd == 0 ? smith_handlers_console_current() : nullptr;
    if (console != nullptr) return console->read(console, buffer, size);
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    if (files != nullptr) return files->read(files, fd, buffer, size);
    SmithIoRequest request = {.operation = SmithIoOperation_Read, .fd = fd, .buffer = buffer, .size = size};
    return smith_handlers_route(&request) ? request.result : read(fd, buffer, size);
}

static ssize_t smith_handlers_write(int fd, const void *buffer, size_t size) {
    SmithConsoleHandler *console = fd == 1 || fd == 2 ? smith_handlers_console_current() : nullptr;
    if (console != nullptr) return console->write(console, fd, buffer, size);
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    if (files != nullptr) return files->write(files, fd, buffer, size);
    SmithIoRequest request = {.operation = SmithIoOperation_Write, .fd = fd, .source = buffer, .size = size};
    return smith_handlers_route(&request) ? request.result : write(fd, buffer, size);
}

static ssize_t smith_handlers_pread(int fd, void *buffer, size_t size, off_t offset) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Pread, .fd = fd, .buffer = buffer, .size = size, .offset = offset};
    return smith_handlers_route(&request) ? request.result : pread(fd, buffer, size, offset);
}

static ssize_t smith_handlers_pwrite(int fd, const void *buffer, size_t size, off_t offset) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Pwrite, .fd = fd, .source = buffer, .size = size, .offset = offset};
    return smith_handlers_route(&request) ? request.result : pwrite(fd, buffer, size, offset);
}

static ssize_t smith_handlers_readv(int fd, const struct iovec *vectors, int count) {
    SmithIoRequest request = {.operation = SmithIoOperation_Readv, .fd = fd, .vectors = vectors, .count = count};
    return smith_handlers_route(&request) ? request.result : readv(fd, vectors, count);
}

static ssize_t smith_handlers_writev(int fd, const struct iovec *vectors, int count) {
    SmithIoRequest request = {.operation = SmithIoOperation_Writev, .fd = fd, .vectors = vectors, .count = count};
    return smith_handlers_route(&request) ? request.result : writev(fd, vectors, count);
}

static ssize_t smith_handlers_send(int fd, const void *buffer, size_t size, int flags) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Send, .fd = fd, .source = buffer, .size = size, .flags = flags};
    return smith_handlers_route(&request) ? request.result : send(fd, buffer, size, flags);
}

static ssize_t smith_handlers_recv(int fd, void *buffer, size_t size, int flags) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Recv, .fd = fd, .buffer = buffer, .size = size, .flags = flags};
    return smith_handlers_route(&request) ? request.result : recv(fd, buffer, size, flags);
}

static int smith_handlers_accept(int fd, struct sockaddr *address, socklen_t *address_length) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Accept, .fd = fd, .address = address, .address_length = address_length};
    return smith_handlers_route(&request) ? (int)request.result : accept(fd, address, address_length);
}

static int smith_handlers_connect(int fd, const struct sockaddr *peer, socklen_t peer_length) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Connect, .fd = fd, .peer = peer, .peer_length = peer_length};
    return smith_handlers_route(&request) ? (int)request.result : connect(fd, peer, peer_length);
}

static int smith_handlers_close(int fd) {
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
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
    SmithConsoleHandler *console = smith_handlers_console_current();
    return (int)(console != nullptr ? console->write(console, fd, data, (size_t)size) : write(fd, data, (size_t)size));
}

static int smith_handlers_console_read(void *cookie, char *data, int size) {
    (void)cookie;
    SmithConsoleHandler *console = smith_handlers_console_current();
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

// With no handler set it is libc's own puts. A program with ASan gets verify_interceptors=0 from the build (ASan's
// start-up check wants dlsym("puts") inside its runtime, which this tuple makes false; build_runtime.c).
static int smith_handlers_puts(const char *text) {
    if (smith_handlers_console_current() == nullptr) return puts(text);
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
    SmithFileHandler *files;
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

static FILE *smith_handlers_stream(SmithFileHandler *files, int fd, const char *mode) {
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
    SmithFileHandler *files = smith_handlers_files_current();
    if (files == nullptr) return fopen(path, mode);
    int fd = files->open(files, path, smith_handlers_mode_flags(mode), 0666);
    FILE *stream = fd >= 0 ? smith_handlers_stream(files, fd, mode) : nullptr;
    if (fd >= 0 && stream == nullptr) files->close(files, fd);
    return stream;
}

static FILE *smith_handlers_fdopen(int fd, const char *mode) {
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    return files != nullptr ? smith_handlers_stream(files, fd, mode) : fdopen(fd, mode);
}

// With a files handler set the stream is a new one (a funopen stream cannot take the place of the old FILE): the old
// stream is closed unless it is a standard one, and the result is the new stream.
static FILE *smith_handlers_freopen(const char *path, const char *mode, FILE *stream) {
    if (smith_handlers_files_current() == nullptr || path == nullptr) return freopen(path, mode, stream);
    if (stream != stdin && stream != stdout && stream != stderr) fclose(stream);
    return smith_handlers_fopen(path, mode);
}

// ---- The environment and the clock: getenv, clock_gettime, nanosleep, usleep and sleep.

static char *smith_handlers_getenv(const char *name) {
    SmithEnvHandler *env = smith_handlers_env_current();
    return env != nullptr ? env->get(env, name) : getenv(name);
}

static int smith_handlers_getentropy(void *buffer, size_t size) {
    SmithEntropyHandler *entropy = smith_handlers_entropy_current();
    if (entropy == nullptr) return getentropy(buffer, size);
    entropy->fill(entropy, buffer, size);
    return 0;
}

static int smith_handlers_clock_gettime(clockid_t which, struct timespec *time) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    return clock != nullptr ? smith_handlers_clock_read(clock, which, time) : clock_gettime(which, time);
}

static int smith_handlers_nanosleep(const struct timespec *duration, struct timespec *remaining) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock == nullptr) return nanosleep(duration, remaining);
    if (remaining != nullptr) *remaining = (struct timespec){};
    return smith_handlers_clock_sleep(clock, smith_handlers_nanoseconds(duration));
}

static int smith_handlers_usleep(useconds_t microseconds) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock == nullptr) return usleep(microseconds);
    return smith_handlers_clock_sleep(clock, (uint64_t)microseconds * 1000U);
}

static unsigned smith_handlers_sleep(unsigned seconds) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock == nullptr) return sleep(seconds);
    return smith_handlers_clock_sleep(clock, (uint64_t)seconds * 1'000'000'000U) == 0 ? 0 : seconds;
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
    {(uintptr_t)&smith_handlers_pread, (uintptr_t)&pread},
    {(uintptr_t)&smith_handlers_pwrite, (uintptr_t)&pwrite},
    {(uintptr_t)&smith_handlers_readv, (uintptr_t)&readv},
    {(uintptr_t)&smith_handlers_writev, (uintptr_t)&writev},
    {(uintptr_t)&smith_handlers_send, (uintptr_t)&send},
    {(uintptr_t)&smith_handlers_recv, (uintptr_t)&recv},
    {(uintptr_t)&smith_handlers_accept, (uintptr_t)&accept},
    {(uintptr_t)&smith_handlers_connect, (uintptr_t)&connect},
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
    {(uintptr_t)&smith_handlers_getenv, (uintptr_t)&getenv},
    {(uintptr_t)&smith_handlers_getentropy, (uintptr_t)&getentropy},
    {(uintptr_t)&smith_handlers_clock_gettime, (uintptr_t)&clock_gettime},
    {(uintptr_t)&smith_handlers_nanosleep, (uintptr_t)&nanosleep},
    {(uintptr_t)&smith_handlers_usleep, (uintptr_t)&usleep},
    {(uintptr_t)&smith_handlers_sleep, (uintptr_t)&sleep},
    {(uintptr_t)&smith_handlers_mutex_lock, (uintptr_t)&pthread_mutex_lock},
    {(uintptr_t)&smith_handlers_mutex_trylock, (uintptr_t)&pthread_mutex_trylock},
    {(uintptr_t)&smith_handlers_mutex_unlock, (uintptr_t)&pthread_mutex_unlock},
};
// clang-format on
#pragma clang diagnostic pop
#else
// ---- Linux (issue #305). An ELF program's own definitions win over a shared library's, so the runtime is an object
// linked into each program, and it defines the functions themselves: malloc, write, fwrite... A call from anywhere in
// the process (the program, a library, one loaded with dlopen) reaches them. Each calls the next one:
// AddressSanitizer's interceptor when the program is sanitized (its malloc, write... are weak aliases of
// ___interceptor_malloc...), else glibc's: __libc_malloc and its family for memory, which ask nothing of the loader,
// and the rest through dlsym(RTLD_NEXT), whose own allocation reaches __libc_calloc. Every call this file makes to a
// function it defines goes to the next one, never to itself.

// NOLINTBEGIN(bugprone-reserved-identifier,readability-identifier-naming): glibc's and AddressSanitizer's own names
extern void *__libc_malloc(size_t size);
extern void *__libc_calloc(size_t count, size_t size);
extern void *__libc_realloc(void *block, size_t size);
extern void __libc_free(void *block);
extern void *__libc_memalign(size_t align, size_t size);
extern void *__libc_valloc(size_t size);

// AddressSanitizer's entry points, null in a program without it.
[[gnu::weak]] extern void *___interceptor_malloc(size_t size);
[[gnu::weak]] extern void *___interceptor_calloc(size_t count, size_t size);
[[gnu::weak]] extern void *___interceptor_realloc(void *block, size_t size);
[[gnu::weak]] extern void ___interceptor_free(void *block);
[[gnu::weak]] extern int ___interceptor_posix_memalign(void **result, size_t align, size_t size);
[[gnu::weak]] extern void *___interceptor_aligned_alloc(size_t align, size_t size);
[[gnu::weak]] extern void *___interceptor_valloc(size_t size);
[[gnu::weak]] extern size_t ___interceptor_malloc_usable_size(const void *block);
// NOLINTEND(bugprone-reserved-identifier,readability-identifier-naming)

static void *smith_handlers_next_malloc(size_t size) {
    return ___interceptor_malloc != nullptr ? ___interceptor_malloc(size) : __libc_malloc(size);
}

static void *smith_handlers_next_calloc(size_t count, size_t size) {
    return ___interceptor_calloc != nullptr ? ___interceptor_calloc(count, size) : __libc_calloc(count, size);
}

static void *smith_handlers_next_realloc(void *block, size_t size) {
    return ___interceptor_realloc != nullptr ? ___interceptor_realloc(block, size) : __libc_realloc(block, size);
}

static void smith_handlers_next_free(void *block) {
    if (___interceptor_free != nullptr) {
        ___interceptor_free(block);
    } else {
        __libc_free(block);
    }
}

static int smith_handlers_next_posix_memalign(void **result, size_t align, size_t size) {
    if (___interceptor_posix_memalign != nullptr) return ___interceptor_posix_memalign(result, align, size);
    if (align < sizeof(void *) || (align & (align - 1)) != 0) return EINVAL;
    void *block = __libc_memalign(align, size);
    if (block == nullptr) return ENOMEM;
    *result = block;
    return 0;
}

static void *smith_handlers_next_aligned_alloc(size_t align, size_t size) {
    return ___interceptor_aligned_alloc != nullptr ? ___interceptor_aligned_alloc(align, size)
                                                   : __libc_memalign(align, size);
}

static void *smith_handlers_next_valloc(size_t size) {
    return ___interceptor_valloc != nullptr ? ___interceptor_valloc(size) : __libc_valloc(size);
}

// A function of the next definition, as a generic function pointer that a call casts back to its own type.
typedef void (*SmithHandlersFunction)(void);

// The interceptors of AddressSanitizer for the rest, null without it. Only their addresses are taken, so the type each
// is declared with here is the generic one.
// NOLINTBEGIN(bugprone-reserved-identifier,readability-identifier-naming): AddressSanitizer's own names
[[gnu::weak]] extern void ___interceptor_open(void);
[[gnu::weak]] extern void ___interceptor_read(void);
[[gnu::weak]] extern void ___interceptor_write(void);
[[gnu::weak]] extern void ___interceptor_pread(void);
[[gnu::weak]] extern void ___interceptor_pwrite(void);
[[gnu::weak]] extern void ___interceptor_readv(void);
[[gnu::weak]] extern void ___interceptor_writev(void);
[[gnu::weak]] extern void ___interceptor_send(void);
[[gnu::weak]] extern void ___interceptor_recv(void);
[[gnu::weak]] extern void ___interceptor_accept(void);
[[gnu::weak]] extern void ___interceptor_connect(void);
[[gnu::weak]] extern void ___interceptor_close(void);
[[gnu::weak]] extern void ___interceptor_syscall(void);
[[gnu::weak]] extern void ___interceptor_fwrite(void);
[[gnu::weak]] extern void ___interceptor_fread(void);
[[gnu::weak]] extern void ___interceptor_fgets(void);
[[gnu::weak]] extern void ___interceptor_fgetc(void);
[[gnu::weak]] extern void ___interceptor_ungetc(void);
[[gnu::weak]] extern void ___interceptor_fputs(void);
[[gnu::weak]] extern void ___interceptor_fputc(void);
[[gnu::weak]] extern void ___interceptor_puts(void);
[[gnu::weak]] extern void ___interceptor_vfprintf(void);
[[gnu::weak]] extern void ___interceptor_vdprintf(void);
[[gnu::weak]] extern void ___interceptor_fflush(void);
[[gnu::weak]] extern void ___interceptor_fopen(void);
[[gnu::weak]] extern void ___interceptor_fclose(void);
[[gnu::weak]] extern void ___interceptor_vfscanf(void);
[[gnu::weak]] extern void ___interceptor_getline(void);
[[gnu::weak]] extern void ___interceptor_getdelim(void);
[[gnu::weak]] extern void ___interceptor_fseek(void);
[[gnu::weak]] extern void ___interceptor_fdopen(void);
[[gnu::weak]] extern void ___interceptor_freopen(void);
[[gnu::weak]] extern void ___interceptor_getenv(void);
[[gnu::weak]] extern void ___interceptor_clock_gettime(void);
[[gnu::weak]] extern void ___interceptor___vfprintf_chk(void);
[[gnu::weak]] extern void ___interceptor___vdprintf_chk(void);
[[gnu::weak]] extern void ___interceptor___fgets_chk(void);
[[gnu::weak]] extern void ___interceptor___fread_chk(void);
[[gnu::weak]] extern void ___interceptor___read_chk(void);
[[gnu::weak]] extern void ___interceptor_nanosleep(void);
[[gnu::weak]] extern void ___interceptor_usleep(void);
[[gnu::weak]] extern void ___interceptor_sleep(void);
// NOLINTEND(bugprone-reserved-identifier,readability-identifier-naming)

// The next definition of a function after this program's: AddressSanitizer's interceptor when the program has it,
// else glibc's (dlsym(RTLD_NEXT)), found the first time it is called (a race finds the same address twice).
typedef struct {
    const char *name;
    SmithHandlersFunction interceptor;
    _Atomic(SmithHandlersFunction) address;
} SmithHandlersNext;

static SmithHandlersFunction smith_handlers_find(SmithHandlersNext *next) {
    if (next->interceptor != nullptr) return next->interceptor;
    SmithHandlersFunction address = atomic_load_explicit(&next->address, memory_order_acquire);
    if (address == nullptr) {
        void *found = dlsym(RTLD_NEXT, next->name);
        if (found == nullptr) abort(); // glibc defines every one of them
        memcpy((void *)&address, (const void *)&found, sizeof address);
        atomic_store_explicit(&next->address, address, memory_order_release);
    }
    return address;
}

static SmithHandlersNext smith_handlers_next_mutex_lock = {"pthread_mutex_lock", nullptr, nullptr};
static SmithHandlersNext smith_handlers_next_mutex_trylock = {"pthread_mutex_trylock", nullptr, nullptr};
static SmithHandlersNext smith_handlers_next_mutex_unlock = {"pthread_mutex_unlock", nullptr, nullptr};
static SmithHandlersNext smith_handlers_next_open = {"open", ___interceptor_open, nullptr};
static SmithHandlersNext smith_handlers_next_read = {"read", ___interceptor_read, nullptr};
static SmithHandlersNext smith_handlers_next_write = {"write", ___interceptor_write, nullptr};
static SmithHandlersNext smith_handlers_next_pread = {"pread", ___interceptor_pread, nullptr};
static SmithHandlersNext smith_handlers_next_pwrite = {"pwrite", ___interceptor_pwrite, nullptr};
static SmithHandlersNext smith_handlers_next_readv = {"readv", ___interceptor_readv, nullptr};
static SmithHandlersNext smith_handlers_next_writev = {"writev", ___interceptor_writev, nullptr};
static SmithHandlersNext smith_handlers_next_send = {"send", ___interceptor_send, nullptr};
static SmithHandlersNext smith_handlers_next_recv = {"recv", ___interceptor_recv, nullptr};
static SmithHandlersNext smith_handlers_next_accept = {"accept", ___interceptor_accept, nullptr};
static SmithHandlersNext smith_handlers_next_connect = {"connect", ___interceptor_connect, nullptr};
static SmithHandlersNext smith_handlers_next_getenv = {"getenv", ___interceptor_getenv, nullptr};
static SmithHandlersNext smith_handlers_next_getentropy = {"getentropy", nullptr, nullptr};
static SmithHandlersNext smith_handlers_next_clock_gettime = {"clock_gettime", ___interceptor_clock_gettime, nullptr};
static SmithHandlersNext smith_handlers_next_nanosleep = {"nanosleep", ___interceptor_nanosleep, nullptr};
static SmithHandlersNext smith_handlers_next_usleep = {"usleep", ___interceptor_usleep, nullptr};
static SmithHandlersNext smith_handlers_next_sleep = {"sleep", ___interceptor_sleep, nullptr};
static SmithHandlersNext smith_handlers_next_close = {"close", ___interceptor_close, nullptr};
static SmithHandlersNext smith_handlers_next_syscall = {"syscall", ___interceptor_syscall, nullptr};
static SmithHandlersNext smith_handlers_next_fwrite = {"fwrite", ___interceptor_fwrite, nullptr};
static SmithHandlersNext smith_handlers_next_fread = {"fread", ___interceptor_fread, nullptr};
static SmithHandlersNext smith_handlers_next_fgets = {"fgets", ___interceptor_fgets, nullptr};
static SmithHandlersNext smith_handlers_next_fgetc = {"fgetc", ___interceptor_fgetc, nullptr};
static SmithHandlersNext smith_handlers_next_ungetc = {"ungetc", ___interceptor_ungetc, nullptr};
static SmithHandlersNext smith_handlers_next_fputs = {"fputs", ___interceptor_fputs, nullptr};
static SmithHandlersNext smith_handlers_next_fputc = {"fputc", ___interceptor_fputc, nullptr};
static SmithHandlersNext smith_handlers_next_puts = {"puts", ___interceptor_puts, nullptr};
static SmithHandlersNext smith_handlers_next_vfprintf = {"vfprintf", ___interceptor_vfprintf, nullptr};
static SmithHandlersNext smith_handlers_next_vdprintf = {"vdprintf", ___interceptor_vdprintf, nullptr};
static SmithHandlersNext smith_handlers_next_fflush = {"fflush", ___interceptor_fflush, nullptr};
static SmithHandlersNext smith_handlers_next_vfprintf_chk = {"__vfprintf_chk", ___interceptor___vfprintf_chk, nullptr};
static SmithHandlersNext smith_handlers_next_vdprintf_chk = {"__vdprintf_chk", ___interceptor___vdprintf_chk, nullptr};
static SmithHandlersNext smith_handlers_next_fgets_chk = {"__fgets_chk", ___interceptor___fgets_chk, nullptr};
static SmithHandlersNext smith_handlers_next_fread_chk = {"__fread_chk", ___interceptor___fread_chk, nullptr};
static SmithHandlersNext smith_handlers_next_read_chk = {"__read_chk", ___interceptor___read_chk, nullptr};
static SmithHandlersNext smith_handlers_next_fopen = {"fopen", ___interceptor_fopen, nullptr};
static SmithHandlersNext smith_handlers_next_fclose = {"fclose", ___interceptor_fclose, nullptr};
static SmithHandlersNext smith_handlers_next_vfscanf = {"vfscanf", ___interceptor_vfscanf, nullptr};
static SmithHandlersNext smith_handlers_next_getline = {"getline", ___interceptor_getline, nullptr};
static SmithHandlersNext smith_handlers_next_getdelim = {"getdelim", ___interceptor_getdelim, nullptr};
static SmithHandlersNext smith_handlers_next_fseek = {"fseek", ___interceptor_fseek, nullptr};
static SmithHandlersNext smith_handlers_next_fdopen = {"fdopen", ___interceptor_fdopen, nullptr};
static SmithHandlersNext smith_handlers_next_freopen = {"freopen", ___interceptor_freopen, nullptr};

// ---- Memory.

void *malloc(size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, smith_handlers_align)
                                : smith_handlers_next_malloc(size);
}

void *calloc(size_t count, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return smith_handlers_next_calloc(count, size);
    size_t bytes = 0;
    if (__builtin_mul_overflow(count, size, &bytes)) {
        errno = ENOMEM;
        return nullptr;
    }
    void *block = allocator->alloc(allocator, bytes, smith_handlers_align);
    if (block != nullptr) memset(block, 0, bytes);
    return block;
}

void *realloc(void *block, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return smith_handlers_next_realloc(block, size);
    if (block == nullptr) return allocator->alloc(allocator, size, smith_handlers_align);
    if (smith_handlers_owns(allocator, block)) return allocator->realloc(allocator, block, size, smith_handlers_align);
    return smith_handlers_next_realloc(block, size);
}

void free(void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (smith_handlers_owns(allocator, block)) {
        allocator->free(allocator, block);
    } else {
        smith_handlers_next_free(block);
    }
}

int posix_memalign(void **result, size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return smith_handlers_next_posix_memalign(result, align, size);
    if (align < sizeof(void *) || (align & (align - 1)) != 0) return EINVAL;
    void *block = allocator->alloc(allocator, size, align);
    if (block == nullptr) return ENOMEM;
    *result = block;
    return 0;
}

void *aligned_alloc(size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return smith_handlers_next_aligned_alloc(align, size);
    return allocator->alloc(allocator, size, align);
}

void *valloc(size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, 4096) : smith_handlers_next_valloc(size);
}

size_t malloc_usable_size(void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (smith_handlers_owns(allocator, block))
        return allocator->size != nullptr ? allocator->size(allocator, block) : 0;
    if (___interceptor_malloc_usable_size != nullptr) return ___interceptor_malloc_usable_size(block);
    void *found = dlsym(RTLD_NEXT, "malloc_usable_size");
    size_t (*next)(void *) = nullptr;
    memcpy((void *)&next, (const void *)&found, sizeof next);
    return next != nullptr ? next(block) : 0;
}

// ---- The file descriptors: 0 is the console's input, 1 and 2 its output, 3 and up the files'.

constexpr int smith_handlers_first_file = 3;

static ssize_t smith_handlers_raw_read(int fd, void *buffer, size_t size) {
    return ((ssize_t (*)(int, void *, size_t))smith_handlers_find(&smith_handlers_next_read))(fd, buffer, size);
}

static ssize_t smith_handlers_raw_write(int fd, const void *buffer, size_t size) {
    return ((ssize_t (*)(int, const void *, size_t))smith_handlers_find(&smith_handlers_next_write))(fd, buffer, size);
}

int open(const char *path, int flags, ...) {
    int mode = 0;
    if (smith_handlers_has(flags, O_CREAT) || smith_handlers_has(flags, O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    SmithFileHandler *files = smith_handlers_files_current();
    if (files != nullptr) return files->open(files, path, flags, mode);
    return ((int (*)(const char *, int, ...))smith_handlers_find(&smith_handlers_next_open))(path, flags, mode);
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    int result = ((int (*)(pthread_mutex_t *))smith_handlers_find(&smith_handlers_next_mutex_lock))(mutex);
    if (result == 0) smith_handlers_lock_taken();
    return result;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    int result = ((int (*)(pthread_mutex_t *))smith_handlers_find(&smith_handlers_next_mutex_trylock))(mutex);
    if (result == 0) smith_handlers_lock_taken();
    return result;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    smith_handlers_lock_given();
    return ((int (*)(pthread_mutex_t *))smith_handlers_find(&smith_handlers_next_mutex_unlock))(mutex);
}

ssize_t read(int fd, void *buffer, size_t size) {
    SmithConsoleHandler *console = fd == 0 ? smith_handlers_console_current() : nullptr;
    if (console != nullptr) return console->read(console, buffer, size);
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    if (files != nullptr) return files->read(files, fd, buffer, size);
    SmithIoRequest request = {.operation = SmithIoOperation_Read, .fd = fd, .buffer = buffer, .size = size};
    return smith_handlers_route(&request) ? request.result : smith_handlers_raw_read(fd, buffer, size);
}

ssize_t write(int fd, const void *buffer, size_t size) {
    SmithConsoleHandler *console = fd == 1 || fd == 2 ? smith_handlers_console_current() : nullptr;
    if (console != nullptr) return console->write(console, fd, buffer, size);
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    if (files != nullptr) return files->write(files, fd, buffer, size);
    SmithIoRequest request = {.operation = SmithIoOperation_Write, .fd = fd, .source = buffer, .size = size};
    return smith_handlers_route(&request) ? request.result : smith_handlers_raw_write(fd, buffer, size);
}

ssize_t pread(int fd, void *buffer, size_t size, off_t offset) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Pread, .fd = fd, .buffer = buffer, .size = size, .offset = offset};
    if (smith_handlers_route(&request)) return request.result;
    return ((ssize_t (*)(int, void *, size_t, off_t))smith_handlers_find(&smith_handlers_next_pread))(fd, buffer, size,
                                                                                                      offset);
}

ssize_t pwrite(int fd, const void *buffer, size_t size, off_t offset) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Pwrite, .fd = fd, .source = buffer, .size = size, .offset = offset};
    if (smith_handlers_route(&request)) return request.result;
    return ((ssize_t (*)(int, const void *, size_t, off_t))smith_handlers_find(&smith_handlers_next_pwrite))(
        fd, buffer, size, offset);
}

ssize_t readv(int fd, const struct iovec *vectors, int count) {
    SmithIoRequest request = {.operation = SmithIoOperation_Readv, .fd = fd, .vectors = vectors, .count = count};
    if (smith_handlers_route(&request)) return request.result;
    return ((ssize_t (*)(int, const struct iovec *, int))smith_handlers_find(&smith_handlers_next_readv))(fd, vectors,
                                                                                                          count);
}

ssize_t writev(int fd, const struct iovec *vectors, int count) {
    SmithIoRequest request = {.operation = SmithIoOperation_Writev, .fd = fd, .vectors = vectors, .count = count};
    if (smith_handlers_route(&request)) return request.result;
    return ((ssize_t (*)(int, const struct iovec *, int))smith_handlers_find(&smith_handlers_next_writev))(fd, vectors,
                                                                                                           count);
}

ssize_t send(int fd, const void *buffer, size_t size, int flags) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Send, .fd = fd, .source = buffer, .size = size, .flags = flags};
    if (smith_handlers_route(&request)) return request.result;
    return ((ssize_t (*)(int, const void *, size_t, int))smith_handlers_find(&smith_handlers_next_send))(fd, buffer,
                                                                                                         size, flags);
}

ssize_t recv(int fd, void *buffer, size_t size, int flags) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Recv, .fd = fd, .buffer = buffer, .size = size, .flags = flags};
    if (smith_handlers_route(&request)) return request.result;
    return ((ssize_t (*)(int, void *, size_t, int))smith_handlers_find(&smith_handlers_next_recv))(fd, buffer, size,
                                                                                                   flags);
}

int accept(int fd, struct sockaddr *address, socklen_t *address_length) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Accept, .fd = fd, .address = address, .address_length = address_length};
    if (smith_handlers_route(&request)) return (int)request.result;
    return ((int (*)(int, struct sockaddr *, socklen_t *))smith_handlers_find(&smith_handlers_next_accept))(
        fd, address, address_length);
}

int connect(int fd, const struct sockaddr *peer, socklen_t peer_length) {
    SmithIoRequest request = {
        .operation = SmithIoOperation_Connect, .fd = fd, .peer = peer, .peer_length = peer_length};
    if (smith_handlers_route(&request)) return (int)request.result;
    return ((int (*)(int, const struct sockaddr *, socklen_t))smith_handlers_find(&smith_handlers_next_connect))(
        fd, peer, peer_length);
}

int close(int fd) {
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    if (files != nullptr) return files->close(files, fd);
    return ((int (*)(int))smith_handlers_find(&smith_handlers_next_close))(fd);
}

// syscall(SYS_read, ...) and syscall(SYS_write, ...) are read and write; every other number is the kernel's.
long syscall(long number, ...) {
    long argument[6] = {};
    va_list args;
    va_start(args, number);
    for (size_t i = 0; i < 6; i++)
        argument[i] = va_arg(args, long);
    va_end(args);
    if (number == SYS_read) return read((int)argument[0], (void *)argument[1], (size_t)argument[2]);
    if (number == SYS_write) return write((int)argument[0], (const void *)argument[1], (size_t)argument[2]);
    return ((long (*)(long, ...))smith_handlers_find(&smith_handlers_next_syscall))(
        number, argument[0], argument[1], argument[2], argument[3], argument[4], argument[5]);
}

// ---- stdio. glibc writes stdout and reads stdin through internal entry points, so while a console is set the
// standard streams are other streams, made with fopencookie over the console's functions, and each stdio function that
// takes a stream (or means stdout or stdin) is called on the stand-in, as on macOS.

static FILE *smith_handlers_console_streams[3]; // stdin, stdout, stderr
static pthread_once_t smith_handlers_console_once = PTHREAD_ONCE_INIT;

static ssize_t smith_handlers_console_write(void *cookie, const char *data, size_t size) {
    int fd = (int)(intptr_t)cookie;
    SmithConsoleHandler *console = smith_handlers_console_current();
    return console != nullptr ? console->write(console, fd, data, size) : smith_handlers_raw_write(fd, data, size);
}

static ssize_t smith_handlers_console_read(void *cookie, char *data, size_t size) {
    (void)cookie;
    SmithConsoleHandler *console = smith_handlers_console_current();
    return console != nullptr ? console->read(console, data, size) : smith_handlers_raw_read(0, data, size);
}

static void smith_handlers_console_make(void) {
    smith_handlers_console_streams[0] =
        fopencookie(nullptr, "r", (cookie_io_functions_t){.read = smith_handlers_console_read});
    for (int fd = 1; fd <= 2; fd++) {
        FILE *out =
            fopencookie((void *)(intptr_t)fd, "w", (cookie_io_functions_t){.write = smith_handlers_console_write});
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

size_t fwrite(const void *data, size_t size, size_t count, FILE *stream) {
    return ((size_t (*)(const void *, size_t, size_t, FILE *))smith_handlers_find(&smith_handlers_next_fwrite))(
        data, size, count, smith_handlers_map(stream));
}

size_t fread(void *data, size_t size, size_t count, FILE *stream) {
    return ((size_t (*)(void *, size_t, size_t, FILE *))smith_handlers_find(&smith_handlers_next_fread))(
        data, size, count, smith_handlers_map(stream));
}

char *fgets(char *line, int size, FILE *stream) {
    return ((char *(*)(char *, int, FILE *))smith_handlers_find(&smith_handlers_next_fgets))(
        line, size, smith_handlers_map(stream));
}

int fgetc(FILE *stream) {
    return ((int (*)(FILE *))smith_handlers_find(&smith_handlers_next_fgetc))(smith_handlers_map(stream));
}

int getc(FILE *stream) {
    return fgetc(stream);
}

// getchar, putchar, vprintf and getline are glibc's extern inline functions when <stdio.h> is read optimized, so these
// are defined by other names, given theirs with an asm label.
int smith_handlers_getchar(void) __asm__("getchar");
int smith_handlers_getchar(void) {
    return fgetc(stdin);
}

int ungetc(int c, FILE *stream) {
    return ((int (*)(int, FILE *))smith_handlers_find(&smith_handlers_next_ungetc))(c, smith_handlers_map(stream));
}

int fputs(const char *text, FILE *stream) {
    return ((int (*)(const char *, FILE *))smith_handlers_find(&smith_handlers_next_fputs))(text,
                                                                                            smith_handlers_map(stream));
}

int fputc(int c, FILE *stream) {
    return ((int (*)(int, FILE *))smith_handlers_find(&smith_handlers_next_fputc))(c, smith_handlers_map(stream));
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

int smith_handlers_putchar(int c) __asm__("putchar");
int smith_handlers_putchar(int c) {
    return fputc(c, stdout);
}

int puts(const char *text) {
    if (smith_handlers_console_current() == nullptr)
        return ((int (*)(const char *))smith_handlers_find(&smith_handlers_next_puts))(text);
    FILE *out = smith_handlers_map(stdout);
    return fputs(text, out) < 0 || fputc('\n', out) < 0 ? EOF : 1;
}

[[gnu::format(printf, 2, 0)]] int vfprintf(FILE *stream, const char *format, va_list args) {
    return ((int (*)(FILE *, const char *, va_list))smith_handlers_find(&smith_handlers_next_vfprintf))(
        smith_handlers_map(stream), format, args);
}

[[gnu::format(printf, 2, 3)]] int fprintf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vfprintf(stream, format, args);
    va_end(args);
    return written;
}

[[gnu::format(printf, 1, 0)]] int smith_handlers_vprintf(const char *format, va_list args) __asm__("vprintf");
[[gnu::format(printf, 1, 0)]] int smith_handlers_vprintf(const char *format, va_list args) {
    return vfprintf(stdout, format, args);
}

[[gnu::format(printf, 1, 2)]] int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vfprintf(stdout, format, args);
    va_end(args);
    return written;
}

[[gnu::format(printf, 2, 0)]] int vdprintf(int fd, const char *format, va_list args) {
    bool console = (fd == 1 || fd == 2) && smith_handlers_console_current() != nullptr;
    if (console) return vfprintf(fd == 1 ? stdout : stderr, format, args);
    return ((int (*)(int, const char *, va_list))smith_handlers_find(&smith_handlers_next_vdprintf))(fd, format, args);
}

[[gnu::format(printf, 2, 3)]] int dprintf(int fd, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vdprintf(fd, format, args);
    va_end(args);
    return written;
}

// ---- The fortified forms. A program built with _FORTIFY_SOURCE calls __printf_chk and its kind for the stdio
// functions above; each does what its plain form does here, and with no handler set glibc's own _chk (ASan's when the
// program has it), so the fortification's checks still run.

// NOLINTBEGIN(bugprone-reserved-identifier,readability-identifier-naming): glibc's names
int __vfprintf_chk(FILE *stream, int flag, const char *format, va_list args) {
    return ((int (*)(FILE *, int, const char *, va_list))smith_handlers_find(&smith_handlers_next_vfprintf_chk))(
        smith_handlers_map(stream), flag, format, args);
}

int __fprintf_chk(FILE *stream, int flag, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = __vfprintf_chk(stream, flag, format, args);
    va_end(args);
    return written;
}

int __vprintf_chk(int flag, const char *format, va_list args) {
    return __vfprintf_chk(stdout, flag, format, args);
}

int __printf_chk(int flag, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = __vfprintf_chk(stdout, flag, format, args);
    va_end(args);
    return written;
}

int __vdprintf_chk(int fd, int flag, const char *format, va_list args) {
    bool console = (fd == 1 || fd == 2) && smith_handlers_console_current() != nullptr;
    if (console) return __vfprintf_chk(fd == 1 ? stdout : stderr, flag, format, args);
    return ((int (*)(int, int, const char *, va_list))smith_handlers_find(&smith_handlers_next_vdprintf_chk))(
        fd, flag, format, args);
}

int __dprintf_chk(int fd, int flag, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = __vdprintf_chk(fd, flag, format, args);
    va_end(args);
    return written;
}

char *__fgets_chk(char *line, size_t length, int size, FILE *stream) {
    return ((char *(*)(char *, size_t, int, FILE *))smith_handlers_find(&smith_handlers_next_fgets_chk))(
        line, length, size, smith_handlers_map(stream));
}

size_t __fread_chk(void *data, size_t length, size_t size, size_t count, FILE *stream) {
    return ((size_t (*)(void *, size_t, size_t, size_t, FILE *))smith_handlers_find(&smith_handlers_next_fread_chk))(
        data, length, size, count, smith_handlers_map(stream));
}

// A read that overruns its buffer is glibc's to report; every other one is read.
ssize_t __read_chk(int fd, void *buffer, size_t size, size_t length) {
    if (size > length)
        return ((ssize_t (*)(int, void *, size_t, size_t))smith_handlers_find(&smith_handlers_next_read_chk))(
            fd, buffer, size, length);
    return read(fd, buffer, size);
}
// NOLINTEND(bugprone-reserved-identifier,readability-identifier-naming)

int fflush(FILE *stream) {
    return ((int (*)(FILE *))smith_handlers_find(&smith_handlers_next_fflush))(
        stream == nullptr ? nullptr : smith_handlers_map(stream));
}

static int smith_handlers_vfscanf(FILE *stream, const char *format, va_list args) {
    return ((int (*)(FILE *, const char *, va_list))smith_handlers_find(&smith_handlers_next_vfscanf))(
        smith_handlers_map(stream), format, args);
}

int fscanf(FILE *stream, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int matched = smith_handlers_vfscanf(stream, format, args);
    va_end(args);
    return matched;
}

int scanf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int matched = smith_handlers_vfscanf(stdin, format, args);
    va_end(args);
    return matched;
}

ssize_t smith_handlers_getline(char **line, size_t *capacity, FILE *stream) __asm__("getline");
ssize_t smith_handlers_getline(char **line, size_t *capacity, FILE *stream) {
    return ((ssize_t (*)(char **, size_t *, FILE *))smith_handlers_find(&smith_handlers_next_getline))(
        line, capacity, smith_handlers_map(stream));
}

// An optimized getline is glibc's inline call of __getdelim, so getdelim and __getdelim are the stream's too.
ssize_t getdelim(char **line, size_t *capacity, int delimiter, FILE *stream) {
    return ((ssize_t (*)(char **, size_t *, int, FILE *))smith_handlers_find(&smith_handlers_next_getdelim))(
        line, capacity, delimiter, smith_handlers_map(stream));
}

ssize_t smith_handlers_getdelim(char **line, size_t *capacity, int delimiter, FILE *stream) __asm__("__getdelim");
ssize_t smith_handlers_getdelim(char **line, size_t *capacity, int delimiter, FILE *stream) {
    return getdelim(line, capacity, delimiter, stream);
}

int fseek(FILE *stream, long offset, int whence) {
    return ((int (*)(FILE *, long, int))smith_handlers_find(&smith_handlers_next_fseek))(smith_handlers_map(stream),
                                                                                         offset, whence);
}

static int smith_handlers_raw_fclose(FILE *stream) {
    return ((int (*)(FILE *))smith_handlers_find(&smith_handlers_next_fclose))(stream);
}

int fclose(FILE *stream) {
    return smith_handlers_raw_fclose(stream);
}

// ---- Files through stdio: while a files handler is set, fopen, fdopen and freopen return an fopencookie stream whose
// callbacks call the handler's read, write and close (a handler has no seek, so such a stream cannot be repositioned).
// The stream keeps the handler that opened it: close it before the handler goes away. Its record is libc's memory.

typedef struct {
    SmithFileHandler *files;
    int fd;
} SmithHandlersFile;

static ssize_t smith_handlers_file_read(void *cookie, char *data, size_t size) {
    const SmithHandlersFile *file = cookie;
    return file->files->read(file->files, file->fd, data, size);
}

static ssize_t smith_handlers_file_write(void *cookie, const char *data, size_t size) {
    const SmithHandlersFile *file = cookie;
    return file->files->write(file->files, file->fd, data, size);
}

static int smith_handlers_file_close(void *cookie) {
    SmithHandlersFile *file = cookie;
    int status = file->files->close(file->files, file->fd);
    smith_handlers_next_free(file);
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

static FILE *smith_handlers_stream(SmithFileHandler *files, int fd, const char *mode) {
    SmithHandlersFile *file = smith_handlers_next_malloc(sizeof *file);
    if (file == nullptr) return nullptr;
    *file = (SmithHandlersFile){files, fd};
    bool readable = mode[0] == 'r' || strchr(mode, '+') != nullptr;
    bool writable = mode[0] != 'r' || strchr(mode, '+') != nullptr;
    FILE *stream = fopencookie(file, mode,
                               (cookie_io_functions_t){.read = readable ? smith_handlers_file_read : nullptr,
                                                       .write = writable ? smith_handlers_file_write : nullptr,
                                                       .close = smith_handlers_file_close});
    if (stream == nullptr) smith_handlers_next_free(file);
    return stream;
}

FILE *fopen(const char *path, const char *mode) {
    SmithFileHandler *files = smith_handlers_files_current();
    if (files == nullptr)
        return ((FILE * (*)(const char *, const char *)) smith_handlers_find(&smith_handlers_next_fopen))(path, mode);
    int fd = files->open(files, path, smith_handlers_mode_flags(mode), 0666);
    FILE *stream = fd >= 0 ? smith_handlers_stream(files, fd, mode) : nullptr;
    if (fd >= 0 && stream == nullptr) files->close(files, fd);
    return stream;
}

FILE *fdopen(int fd, const char *mode) {
    SmithFileHandler *files = fd >= smith_handlers_first_file ? smith_handlers_files_current() : nullptr;
    if (files != nullptr) return smith_handlers_stream(files, fd, mode);
    return ((FILE * (*)(int, const char *)) smith_handlers_find(&smith_handlers_next_fdopen))(fd, mode);
}

// With a files handler set the stream is a new one (a cookie stream cannot take the place of the old FILE): the old
// stream is closed unless it is a standard one, and the result is the new stream.
FILE *freopen(const char *path, const char *mode, FILE *stream) {
    if (smith_handlers_files_current() == nullptr || path == nullptr)
        return ((FILE * (*)(const char *, const char *, FILE *))
                    smith_handlers_find(&smith_handlers_next_freopen))(path, mode, stream);
    if (stream != stdin && stream != stdout && stream != stderr) smith_handlers_raw_fclose(stream);
    return fopen(path, mode);
}
// ---- The environment and the clock: getenv, clock_gettime, nanosleep, usleep and sleep.

char *getenv(const char *name) {
    SmithEnvHandler *env = smith_handlers_env_current();
    if (env != nullptr) return env->get(env, name);
    return ((char *(*)(const char *))smith_handlers_find(&smith_handlers_next_getenv))(name);
}

int getentropy(void *buffer, size_t size) {
    SmithEntropyHandler *entropy = smith_handlers_entropy_current();
    if (entropy == nullptr)
        return ((int (*)(void *, size_t))smith_handlers_find(&smith_handlers_next_getentropy))(buffer, size);
    entropy->fill(entropy, buffer, size);
    return 0;
}

int clock_gettime(clockid_t which, struct timespec *time) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock != nullptr) return smith_handlers_clock_read(clock, which, time);
    return ((int (*)(clockid_t, struct timespec *))smith_handlers_find(&smith_handlers_next_clock_gettime))(which,
                                                                                                            time);
}

int nanosleep(const struct timespec *duration, struct timespec *remaining) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock == nullptr)
        return ((int (*)(const struct timespec *, struct timespec *))smith_handlers_find(
            &smith_handlers_next_nanosleep))(duration, remaining);
    if (remaining != nullptr) *remaining = (struct timespec){};
    return smith_handlers_clock_sleep(clock, smith_handlers_nanoseconds(duration));
}

int usleep(useconds_t microseconds) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock == nullptr) return ((int (*)(useconds_t))smith_handlers_find(&smith_handlers_next_usleep))(microseconds);
    return smith_handlers_clock_sleep(clock, (uint64_t)microseconds * 1000U);
}

unsigned sleep(unsigned seconds) {
    SmithClockHandler *clock = smith_handlers_clock_current();
    if (clock == nullptr) return ((unsigned (*)(unsigned))smith_handlers_find(&smith_handlers_next_sleep))(seconds);
    return smith_handlers_clock_sleep(clock, (uint64_t)seconds * 1'000'000'000U) == 0 ? 0 : seconds;
}
#endif
