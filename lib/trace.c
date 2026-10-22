// smith's stack traces: the walk by frame pointers and the text that smith reads. The design is in
// include/smith/trace.h. Everything here that a signal handler may call uses write(2) and no allocator, formatting
// numbers by hand.
#include "../include/smith/trace.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <sys/ucontext.h>
#endif

void (*smith_failure_hook)(SmithFailureKind kind, const uintptr_t *frames, size_t count);
uintptr_t smith_trace_stop;

constexpr uintptr_t smith_trace_address_mask = 0x0000'7fff'ffff'ffffU; // user space; drops pointer authentication bits
constexpr size_t smith_trace_line_capacity = 1280;
constexpr size_t smith_trace_images_max = 32;
constexpr uintptr_t smith_trace_frame_step_max = 64U << 20; // a frame farther than this from the last is not a frame

// The stack of the running thread, or [0, UINTPTR_MAX] where the system does not say.
typedef struct {
    uintptr_t low;
    uintptr_t high;
} SmithTraceStack;

static SmithTraceStack smith_trace_stack(void) {
#ifdef __APPLE__
    pthread_t thread = pthread_self();
    uintptr_t high = (uintptr_t)pthread_get_stackaddr_np(thread);
    size_t size = pthread_get_stacksize_np(thread);
    if (high > size) return (SmithTraceStack){.low = high - size, .high = high};
#endif
    return (SmithTraceStack){.low = 0, .high = UINTPTR_MAX};
}

// Follows the chain of saved frame pointers from fp, storing each return address after the first used frames. A frame
// pointer that is not aligned, does not rise, jumps too far or leaves the stack ends the walk, so a corrupted stack
// gives a short trace and not a second crash.
static size_t smith_trace_walk(uintptr_t *frames, size_t used, size_t capacity, uintptr_t fp) {
    SmithTraceStack stack = smith_trace_stack();
    uintptr_t previous = 0;
    for (size_t step = 0; step < capacity && used < capacity; step++) { // bounded by the capacity
        bool aligned = fp % sizeof(uintptr_t) == 0;
        bool inside = fp >= stack.low && fp <= stack.high - 2 * sizeof(uintptr_t);
        bool rising = previous == 0 || (fp > previous && fp - previous <= smith_trace_frame_step_max);
        if (fp == 0 || !aligned || !inside || !rising) break;
        const uintptr_t *frame = (const uintptr_t *)fp;
        uintptr_t address = frame[1] & smith_trace_address_mask;
        if (address == 0) break;
        frames[used++] = address;
        previous = fp;
        fp = frame[0];
        if (smith_trace_stop != 0 && fp == smith_trace_stop) break; // that address is in the frame it names: the last
    }
    return used;
}

[[gnu::noinline]] size_t smith_trace_capture(uintptr_t *frames, size_t capacity) {
    if (frames == nullptr || capacity == 0) return 0;
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    // The return address of this function is frames[0]: the walk starts at its own frame.
    return smith_trace_walk(frames, 0, capacity, fp);
}

size_t smith_trace_capture_signal(uintptr_t *frames, size_t capacity, const void *ucontext) {
    if (frames == nullptr || capacity == 0 || ucontext == nullptr) return 0;
#if defined(__APPLE__) && defined(__aarch64__)
    const ucontext_t *context = ucontext;
    uintptr_t pc = (uintptr_t)__darwin_arm_thread_state64_get_pc(context->uc_mcontext->__ss);
    uintptr_t lr = (uintptr_t)__darwin_arm_thread_state64_get_lr(context->uc_mcontext->__ss);
    uintptr_t fp = (uintptr_t)__darwin_arm_thread_state64_get_fp(context->uc_mcontext->__ss);
    frames[0] = pc & smith_trace_address_mask;
    size_t used = 1;
    // A function that has not yet saved its frame (or never does: a leaf) hides its caller in lr; the walk from fp may
    // give the same address next, which is then not repeated.
    uintptr_t next = fp != 0 && fp % sizeof(uintptr_t) == 0 ? ((const uintptr_t *)fp)[1] & smith_trace_address_mask : 0;
    lr &= smith_trace_address_mask;
    if (capacity > 1 && lr != 0 && lr != next && lr != frames[0]) frames[used++] = lr;
    return smith_trace_walk(frames, used, capacity, fp);
#elif defined(__APPLE__) && defined(__x86_64__)
    const ucontext_t *context = ucontext;
    frames[0] = (uintptr_t)context->uc_mcontext->__ss.__rip & smith_trace_address_mask;
    return smith_trace_walk(frames, 1, capacity, (uintptr_t)context->uc_mcontext->__ss.__rbp);
#else
    (void)ucontext;
    return smith_trace_capture(frames, capacity);
#endif
}

// Text, by hand.

typedef struct {
    char text[smith_trace_line_capacity];
    size_t used;
} SmithTraceLine;

static void smith_trace_put(SmithTraceLine *line, const char *text) {
    for (size_t i = 0; text[i] != '\0' && line->used + 1 < sizeof line->text; i++)
        line->text[line->used++] = text[i];
}

static void smith_trace_put_hex(SmithTraceLine *line, uintptr_t value, size_t digits_min) {
    static const char digits[] = "0123456789abcdef";
    char reversed[2 * sizeof(uintptr_t)];
    size_t count = 0;
    for (; (value != 0 || count < digits_min || count == 0) && count < sizeof reversed; count++) {
        reversed[count] = digits[value & 0xFU];
        value >>= 4;
    }
    smith_trace_put(line, "0x");
    while (count > 0 && line->used + 1 < sizeof line->text)
        line->text[line->used++] = reversed[--count];
}

static void smith_trace_flush(int fd, SmithTraceLine *line) {
    if (line->used + 1 < sizeof line->text) line->text[line->used++] = '\n';
    size_t sent = 0;
    for (size_t attempt = 0; sent < line->used && attempt < line->used; attempt++) { // each pass sends at least a byte
        ssize_t got = write(fd, line->text + sent, line->used - sent);
        if (got <= 0) break;
        sent += (size_t)got;
    }
    line->used = 0;
}

// The images the frames fall in.

typedef struct {
    uintptr_t start; // __TEXT, in memory
    uintptr_t size;  // of __TEXT
    uintptr_t slide;
    const char *path;
    char uuid[37]; // "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX", or "-"
} SmithTraceImage;

#ifdef __APPLE__
static void smith_trace_uuid_text(const uint8_t bytes[16], char out[37]) {
    static const char digits[] = "0123456789ABCDEF";
    size_t at = 0;
    for (size_t i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[at++] = '-';
        out[at++] = digits[bytes[i] >> 4];
        out[at++] = digits[bytes[i] & 0xFU];
    }
    out[at] = '\0';
}

// Reads the header's load commands: the UUID and the size of __TEXT. False when it is not a 64-bit Mach-O.
static bool smith_trace_image_read(uint32_t index, SmithTraceImage *image) {
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(index);
    if (header == nullptr || header->magic != MH_MAGIC_64) return false;
    *image =
        (SmithTraceImage){.slide = (uintptr_t)_dyld_get_image_vmaddr_slide(index), .path = _dyld_get_image_name(index)};
    memcpy(image->uuid, "-", 2);
    const uint8_t *at = (const uint8_t *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; i++) {
        const struct load_command *command = (const struct load_command *)at;
        if (command->cmd == LC_UUID) smith_trace_uuid_text(((const struct uuid_command *)command)->uuid, image->uuid);
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            if (strcmp(segment->segname, "__TEXT") == 0) {
                image->size = (uintptr_t)segment->vmsize;
                image->start =
                    (uintptr_t)segment->vmaddr + image->slide; // not the header's: a cache image's is elsewhere
            }
        }
        if (command->cmdsize == 0) break;
        at += command->cmdsize;
    }
    return image->size != 0;
}

// The images any frame falls in, in the order of the frames, each once.
static size_t smith_trace_images(const uintptr_t *frames, size_t count, SmithTraceImage *images, size_t capacity) {
    size_t found = 0;
    uint32_t total = _dyld_image_count();
    for (uint32_t index = 0; index < total && found < capacity; index++) {
        SmithTraceImage image;
        if (!smith_trace_image_read(index, &image)) continue;
        for (size_t i = 0; i < count; i++) {
            if (frames[i] >= image.start && frames[i] - image.start < image.size) {
                images[found++] = image;
                break;
            }
        }
    }
    return found;
}
#else
// Elsewhere the frames are written alone: the symbolizer is given the program's path with --binary.
static size_t smith_trace_images(const uintptr_t *frames, size_t count, SmithTraceImage *images, size_t capacity) {
    (void)frames;
    (void)count;
    (void)images;
    (void)capacity;
    return 0;
}
#endif

static void smith_trace_write_image(int fd, const char *indent, const SmithTraceImage *image) {
    SmithTraceLine line = {};
    smith_trace_put(&line, indent);
    smith_trace_put(&line, "smith-trace image ");
    smith_trace_put(&line, image->uuid);
    smith_trace_put(&line, " ");
    smith_trace_put_hex(&line, image->start, 1);
    smith_trace_put(&line, " ");
    smith_trace_put_hex(&line, image->slide, 1);
    smith_trace_put(&line, " ");
    smith_trace_put_hex(&line, image->size, 1);
    smith_trace_put(&line, " ");
    smith_trace_put(&line, image->path != nullptr ? image->path : "-");
    smith_trace_flush(fd, &line);
}

// " name+0xoff" for the nearest symbol, or nothing.
static void smith_trace_put_name(SmithTraceLine *line, uintptr_t address, bool exact) {
#ifdef __APPLE__
    Dl_info info = {};
    uintptr_t lookup = exact ? address : address - 1; // a return address may be past the end of its function
    if (dladdr((const void *)lookup, &info) == 0 || info.dli_sname == nullptr || info.dli_saddr == nullptr) return;
    smith_trace_put(line, " ");
    smith_trace_put(line, info.dli_sname);
    smith_trace_put(line, "+");
    smith_trace_put_hex(line, address - (uintptr_t)info.dli_saddr, 1);
#else
    (void)line;
    (void)address;
    (void)exact;
#endif
}

void smith_trace_write(int fd, const char *indent, const char *heading, const char *site, const char *details,
                       const uintptr_t *frames, size_t count, bool exact, bool names) {
    SmithTraceLine line = {};
    smith_trace_put(&line, indent);
    smith_trace_put(&line, "smith-trace begin ");
    smith_trace_put(&line, heading != nullptr ? heading : "failed");
    smith_trace_flush(fd, &line);
    if (site != nullptr) {
        smith_trace_put(&line, indent);
        smith_trace_put(&line, "smith-trace site ");
        smith_trace_put(&line, site);
        smith_trace_flush(fd, &line);
    }
    for (const char *at = details; at != nullptr && *at != '\0';) { // one line of the text at a time
        size_t length = strcspn(at, "\n");
        smith_trace_put(&line, indent);
        smith_trace_put(&line, "smith-trace detail ");
        for (size_t i = 0; i < length && line.used + 2 < sizeof line.text; i++)
            line.text[line.used++] = at[i];
        smith_trace_flush(fd, &line);
        at += length + (at[length] == '\n' ? 1 : 0);
    }
    SmithTraceImage images[smith_trace_images_max];
    size_t image_count = smith_trace_images(frames, count, images, smith_trace_images_max);
    for (size_t i = 0; i < image_count; i++)
        smith_trace_write_image(fd, indent, &images[i]);
    for (size_t i = 0; i < count; i++) {
        bool at = exact && i == 0;
        smith_trace_put(&line, indent);
        smith_trace_put(&line, at ? "smith-trace pc " : "smith-trace frame ");
        smith_trace_put_hex(&line, frames[i], 1);
        if (names) smith_trace_put_name(&line, frames[i], at);
        smith_trace_flush(fd, &line);
    }
    smith_trace_put(&line, indent);
    smith_trace_put(&line, "smith-trace end");
    smith_trace_flush(fd, &line);
}

// Failures.

const char *smith_failure_heading(SmithFailureKind kind) {
    switch (kind) {
    case SmithFailure_Check:
        return "check failed";
    case SmithFailure_Requires:
        return "requires failed";
    case SmithFailure_Ensures:
        return "ensures failed";
    case SmithFailure_Invariant:
        return "invariant failed";
    case SmithFailure_Assert:
        return "assert failed";
    case SmithFailure_Capacity:
        return "arena out of capacity";
    }
    return "failed";
}

void smith_failure_detail(SmithFailureKind kind, const char *details) {
    uintptr_t frames[smith_trace_frames_max];
    size_t count = smith_trace_capture(frames, smith_trace_frames_max);
    if (smith_failure_hook != nullptr) smith_failure_hook(kind, frames, count);
    fflush(nullptr);
    smith_trace_write(STDERR_FILENO, "", smith_failure_heading(kind), nullptr, details, frames, count, false, true);
    abort();
}

void smith_failure(SmithFailureKind kind) {
    smith_failure_detail(kind, nullptr);
}
