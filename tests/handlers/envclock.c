// An env handler answers getenv, a clock handler answers clock_gettime, nanosleep, usleep and sleep, and an entropy
// handler answers getentropy, per thread; with none set they are libc's. A sleep on the fake clock advances it and
// returns at once. Exits 0 when every check is ok.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>
#include "smith/handlers.h"

static int failures;

static void check(const char *name, int ok) {
    printf("%s  %s\n", ok ? "ok" : "FAIL", name);
    if (!ok) failures += 1;
}

typedef struct {
    SmithEnvHandler env;
    int asked;
} FakeEnv;

static char *fake_get(SmithEnvHandler *self, const char *name) {
    ((FakeEnv *)self)->asked += 1;
    return strcmp(name, "HOME") == 0 ? "/home/ada" : nullptr;
}

typedef struct {
    SmithClockHandler clock;
    int64_t now;
    uint64_t slept;
} FakeClock;

static int64_t fake_now(SmithClockHandler *self, int which) {
    (void)which;
    return ((FakeClock *)self)->now;
}

static void fake_sleep(SmithClockHandler *self, uint64_t nanoseconds) {
    FakeClock *fake = (FakeClock *)self;
    fake->now += (int64_t)nanoseconds;
    fake->slept += nanoseconds;
}

typedef struct {
    SmithEntropyHandler entropy;
    unsigned char next;
    int asked;
} FakeEntropy;

static void fake_fill(SmithEntropyHandler *self, unsigned char *buffer, size_t size) {
    FakeEntropy *fake = (FakeEntropy *)self;
    fake->asked += 1;
    for (size_t i = 0; i < size; i++)
        buffer[i] = fake->next++;
}

int main(void) {
    check("no env handler: getenv is libc's", getenv("PATH") != nullptr && smith_handlers_env() == nullptr);
    FakeEnv env = {.env = {fake_get}};
    SmithEnvHandler *before_env = smith_handlers_set_env(&env.env);
    check("the setter returns the previous handler", before_env == nullptr && smith_handlers_env() == &env.env);
    char *home = getenv("HOME");
    check("getenv reaches the handler", home != nullptr && strcmp(home, "/home/ada") == 0 && env.asked == 1);
    check("a name the handler does not know is null", getenv("PATH") == nullptr && env.asked == 2);
    smith_handlers_set_env(before_env);
    check("the handler is gone", getenv("PATH") != nullptr && env.asked == 2);

    struct timespec real = {};
    check("no clock handler: clock_gettime is libc's", clock_gettime(CLOCK_MONOTONIC, &real) == 0 && real.tv_sec >= 0);
    FakeClock fake = {.clock = {fake_now, fake_sleep}, .now = 5'000'000'123};
    SmithClockHandler *before_clock = smith_handlers_set_clock(&fake.clock);
    check("the clock setter returns the previous handler", before_clock == nullptr);
    struct timespec time = {};
    check("clock_gettime reaches the handler",
          clock_gettime(CLOCK_REALTIME, &time) == 0 && time.tv_sec == 5 && time.tv_nsec == 123);
    struct timespec duration = {.tv_sec = 3600, .tv_nsec = 7};
    struct timespec remaining = {.tv_sec = 9, .tv_nsec = 9};
    check("nanosleep on the fake advances it and does not block",
          nanosleep(&duration, &remaining) == 0 && remaining.tv_sec == 0 && fake.slept == 3'600'000'000'007);
    check("usleep advances it", usleep(1500) == 0 && fake.slept == 3'600'000'000'007 + 1'500'000);
    check("sleep advances it", sleep(10) == 0 && fake.slept == 3'600'000'000'007 + 1'500'000 + 10'000'000'000);
    check("the fake's time moved with its sleeps", fake.now == 5'000'000'123 + (int64_t)fake.slept);
    smith_handlers_set_clock(before_clock);
    check("the clock handler is gone", smith_handlers_clock() == nullptr && clock_gettime(CLOCK_MONOTONIC, &real) == 0);

    unsigned char bytes[8] = {};
    check("no entropy handler: getentropy is the system's",
          getentropy(bytes, sizeof bytes) == 0 && smith_handlers_entropy() == nullptr);
    FakeEntropy entropy = {.entropy = {fake_fill}, .next = 10};
    SmithEntropyHandler *before_entropy = smith_handlers_set_entropy(&entropy.entropy);
    check("the entropy setter returns the previous handler",
          before_entropy == nullptr && smith_handlers_entropy() == &entropy.entropy);
    check("getentropy reaches the handler",
          getentropy(bytes, sizeof bytes) == 0 && bytes[0] == 10 && bytes[7] == 17 && entropy.asked == 1);
    smith_handlers_set_entropy(before_entropy);
    check("the entropy handler is gone",
          smith_handlers_entropy() == nullptr && getentropy(bytes, sizeof bytes) == 0 && entropy.asked == 1);
    return failures == 0 ? 0 : 1;
}
