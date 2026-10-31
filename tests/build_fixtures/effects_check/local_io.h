#pragma once
// A small capability record of the fixtures' own, standing where the deleted io library stood: one value that main makes.
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} LocalIo;
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} LocalFiles;
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} LocalClock;
typedef struct [[clang::annotate("smith::capability")]] {
    int fd;
} LocalConsole;
typedef struct {
    int seed;
} LocalIoTest;
[[clang::annotate("smith::main_only")]] LocalIo local_io_make(int argc, char **argv);
LocalIo local_io_make_test(LocalIoTest test);
void local_io_close(LocalIo *io);
