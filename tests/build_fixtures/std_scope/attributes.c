[[std::pure]] int identity(int x) { return x; }
[[std::pure]] int forwards(int x) { return identity(x); }
[[std::effects(alloc)]] void *grow(void);
[[std::alloc]] void *grown(void) { return grow(); }
[[std::pure]] void *broken(void) { return grown(); }
