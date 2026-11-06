// A header with no #include, for tests/front_fix_c_headers_test.smith: the fixer reads it without a C library.
#ifndef FIXTURE_SLOT_H
#define FIXTURE_SLOT_H

#define FIXTURE_SLOT_LIMIT 16

typedef struct {
    int used;
    int limit;
} FixtureSlot;

extern int fixture_slot_total;

#endif
