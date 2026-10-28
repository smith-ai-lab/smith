typedef struct { int x; } e_priv_Hidden;
typedef e_priv_Hidden e_priv_Alias;
e_priv_Hidden e_priv_make() __asm__("_e::priv::make");
typedef struct { e_priv_Alias inner; } e_priv_Wrapper;
