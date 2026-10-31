// The handler table of a thread (issue #885, lib/handlers.c): a second SmithHandlers swapped in with
// smith_handlers_set_table is the one write, malloc and the getters see; swapped back, the previous handlers (and
// libc) are in force again; two threads each set a handler and neither sees the other's. Prints `ok name` or `FAIL
// name`.
#include "smith/handlers.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

static void check(const char *name, bool good) {
    printf("%s %s\n", good ? "ok" : "FAIL", name);
    if (!good) failures += 1;
}

typedef struct {
    SmithConsoleHandler console;
    char text[64];
    size_t length;
} FakeConsole;

static ssize_t fake_write(SmithConsoleHandler *self, int fd, const void *buffer, size_t size) {
    FakeConsole *fake = (FakeConsole *)self;
    (void)fd;
    if (fake->length + size < sizeof fake->text) memcpy(fake->text + fake->length, buffer, size);
    fake->length += size;
    return (ssize_t)size;
}

static ssize_t fake_read(SmithConsoleHandler *self, void *buffer, size_t size) {
    (void)self, (void)buffer, (void)size;
    return 0;
}

typedef struct {
    SmithAllocator allocator;
    unsigned allocs;
    unsigned char space[256];
} FakeAllocator;

static void *fake_alloc(SmithAllocator *self, size_t size, size_t align) {
    FakeAllocator *fake = (FakeAllocator *)self;
    (void)align;
    fake->allocs += 1;
    return size <= sizeof fake->space ? fake->space : nullptr;
}

static void *fake_realloc(SmithAllocator *self, void *block, size_t size, size_t align) {
    (void)block;
    return fake_alloc(self, size, align);
}

static void fake_free(SmithAllocator *self, void *block) {
    (void)self, (void)block;
}

static bool fake_owns(SmithAllocator *self, const void *block) {
    return block == ((FakeAllocator *)self)->space;
}

static void say(const char *text) {
    (void)!write(1, text, strlen(text));
}

static void test_swap(void) {
    SmithHandlers *own = smith_handlers_table();
    check("the current table is the thread's own at first", own != nullptr && own == smith_handlers_table());
    FakeConsole console = {.console = {fake_write, fake_read}};
    FakeAllocator allocator = {.allocator = {fake_alloc, fake_realloc, fake_free, fake_owns, nullptr}};
    SmithHandlers second = {.allocator = &allocator.allocator, .console = &console.console};

    // Nothing is printed while the table is swapped in: stdout is the fake console's then.
    SmithHandlers *previous = smith_handlers_set_table(&second);
    bool current = smith_handlers_table() == &second;
    bool getters = smith_handlers_allocator() == &allocator.allocator && smith_handlers_console() == &console.console;
    say("hi");
    void *volatile block = malloc(16);
    bool wrote = console.length == 2 && memcmp(console.text, "hi", 2) == 0;
    bool allocated = block == allocator.space && allocator.allocs == 1;
    free(block);
    SmithFileHandler *files = (SmithFileHandler *)&console; // only its identity is read
    smith_handlers_set_files(files);
    bool member = second.files == files && own->files == nullptr;
    smith_handlers_set_files(nullptr);
    SmithHandlers *swapped = smith_handlers_set_table(previous);

    check("set_table returns the table that was current", previous == own);
    check("the table is the current one", current);
    check("the getters read the current table", getters);
    check("a write reaches the fake console of the table", wrote);
    check("a malloc reaches the fake allocator of the table", allocated);
    check("a setter writes its member of the current table", member);
    check("set_table returns the one it replaced", swapped == &second);
    check("swapped back, the previous handlers are in force: none",
          smith_handlers_allocator() == nullptr && smith_handlers_console() == nullptr);
    void *volatile plain = malloc(16);
    check("swapped back, malloc is libc's", plain != allocator.space && allocator.allocs == 1);
    free(plain);
    check("swapped back, the fake saw no more writes", console.length == 2);
}

static void test_previous_handlers(void) {
    FakeAllocator first = {.allocator = {fake_alloc, fake_realloc, fake_free, fake_owns, nullptr}};
    SmithAllocator *outside = smith_handlers_set_allocator(&first.allocator);
    SmithHandlers empty = {};
    SmithHandlers *own = smith_handlers_set_table(&empty);
    check("a table with no handler is libc's even while the own one has one", smith_handlers_allocator() == nullptr);
    void *volatile plain = malloc(8);
    check("... for malloc", plain != first.space && first.allocs == 0);
    free(plain);
    smith_handlers_set_table(own);
    check("the own table kept its allocator", smith_handlers_allocator() == &first.allocator);
    void *volatile block = malloc(8);
    check("... and malloc reaches it again", block == first.space && first.allocs == 1);
    free(block);
    smith_handlers_set_allocator(outside);
}

typedef struct {
    SmithAllocator allocator;
    bool set_here;
    bool got_own;
    atomic_int *arrived;
} Worker;

// Both threads have set their handler (2), then both have drawn from it (4), before either goes on.
static void rendezvous(atomic_int *arrived, int count) {
    atomic_fetch_add(arrived, 1);
    while (atomic_load(arrived) < count)
        sched_yield();
}

static void *worker(void *argument) {
    Worker *self = argument;
    FakeAllocator mine = {.allocator = {fake_alloc, fake_realloc, fake_free, fake_owns, nullptr}};
    smith_handlers_set_allocator(&mine.allocator);
    rendezvous(self->arrived, 2);
    self->set_here = smith_handlers_allocator() == &mine.allocator;
    void *volatile block = malloc(8);
    self->got_own = block == mine.space && mine.allocs == 1;
    free(block);
    rendezvous(self->arrived, 4);
    smith_handlers_set_allocator(nullptr);
    return nullptr;
}

static void test_threads(void) {
    atomic_int arrived = 0;
    Worker a = {.arrived = &arrived};
    Worker b = {.arrived = &arrived};
    pthread_t one;
    pthread_t two;
    pthread_create(&one, nullptr, worker, &a);
    pthread_create(&two, nullptr, worker, &b);
    pthread_join(one, nullptr);
    pthread_join(two, nullptr);
    check("two threads each set a handler and each sees its own", a.set_here && b.set_here);
    check("... and neither sees the other's", a.got_own && b.got_own);
    check("the main thread has none", smith_handlers_allocator() == nullptr);
}

int main(void) {
    static char
        output[1 << 14]; // stdio would take its buffer from the first handler in force, and keep it past its scope
    setvbuf(stdout, output, _IOFBF, sizeof output);
    test_swap();
    test_previous_handlers();
    test_threads();
    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
