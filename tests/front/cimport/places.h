// The limit of places (the fixture of tests/front_cimport_test.smith, #1941).
#define PLACES_LIMIT 42

/// Adds two numbers.
/// Both are read, neither is written.
int places_add(int a, int b);

int places_bare(void);

/* A point on the plane. */
struct PlacesPoint {
    // The horizontal position.
    int x;
    int y;
};

// A whole number of ticks.
typedef long PlacesTicks;

/** The colors. */
enum PlacesColor {
    // The first one.
    PLACES_RED,
    PLACES_GREEN
};

PlacesTicks places_now(void);
