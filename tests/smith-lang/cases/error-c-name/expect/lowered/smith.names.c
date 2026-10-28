int smith_names_one();
int smith_names_one() { return 1; }
int smith_names_second();
int smith_names_second() { return 2; }
int smith_names_plain();
int smith_names_plain() { return 3; }
int smith_names_plain() __asm__("_smith::names::plain");
int smith_names_plain() { return 4; }
static int smith_names_hidden() { return 5; }
int smith_names_fourth();
int smith_names_fourth() { return 6; }
int smith_names_later();
int smith_names_later() { return 7; }
static int smith_names_later() { return 8; } static int smith_names_sixth() { return 9; } int smith_names_seventh() __asm__("_smith::names::seventh"); int smith_names_seventh() { return 10; } int smith_names_eighth() __asm__("_smith::names::eighth"); int smith_names_eighth() { return 11; } int smith_names_ninth() __asm__("_smith::names::ninth"); int smith_names_ninth() { return 12; } int smith_names_tenth() __asm__("_smith::names::tenth"); int smith_names_tenth() { return 13; }
int smith_names_a();
int smith_names_a() { return 14; }
enum { smith_names_k_Alpha, smith_names_k_Beta };
