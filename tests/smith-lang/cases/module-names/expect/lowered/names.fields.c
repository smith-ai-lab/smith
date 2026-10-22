typedef struct { int count; int total; } names_fields_Stats;
static int names_fields_count = 10;
static int names_fields_total(void) {
return 5;
}
int names_fields_combine(names_fields_Stats *stats) __asm__("_names::fields::combine");
int names_fields_combine(names_fields_Stats *stats) {
int local = stats->count + stats->total;
return local + names_fields_count + names_fields_total();
}
names_fields_Stats names_fields_make(int count, int total) __asm__("_names::fields::make");
names_fields_Stats names_fields_make(int count, int total) {
names_fields_Stats stats = {.count = count, .total = total};
return stats;
}
