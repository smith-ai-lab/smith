// A failing check while another thread is blocked in a read on stdin (#573): the failure must report and abort, not
// wait for stdin's lock. tests/failure_stdin_test.smith compiles and runs it. An alarm ends a regression with SIGALRM.
#include <pthread.h>
#include <smith/trace.h>
#include <stdio.h>
#include <unistd.h>

static void *reader(void *unused) {
    (void)unused;
    (void)getchar(); // holds stdin's lock for as long as the read blocks
    return nullptr;
}

int main(void) {
    alarm(8);
    pthread_t thread;
    pthread_create(&thread, nullptr, reader, nullptr);
    sleep(1); // the reader is in its read
    printf("before\n");
    smith_failure(SmithFailure_Check);
}
