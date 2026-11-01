__extension__({ extern void reflect_show__Pair(main_Pair * x) __asm__("_reflect::show(main_Pair)"); reflect_show__Pair(& pair); })
__extension__({ extern void reflect_show__Mixed(main_Mixed * x) __asm__("_reflect::show(main_Mixed)"); reflect_show__Mixed(& mixed); })
__extension__({ extern double reflect_sum__Pair(main_Pair value) __asm__("_reflect::sum(main_Pair)"); reflect_sum__Pair(pair); })
__extension__({ extern double reflect_sum__Mixed(main_Mixed value) __asm__("_reflect::sum(main_Mixed)"); reflect_sum__Mixed(mixed); })
__extension__({ extern __SIZE_TYPE__ reflect_last_index__Mixed(void) __asm__("_reflect::last_index(main_Mixed)"); reflect_last_index__Mixed(); })
__extension__({ extern __SIZE_TYPE__ reflect_field_count__Pair(main_Pair x) __asm__("_reflect::field_count(main_Pair)"); reflect_field_count__Pair(pair); })
__extension__({ extern __SIZE_TYPE__ reflect_field_count__Mixed(main_Mixed x) __asm__("_reflect::field_count(main_Mixed)"); reflect_field_count__Mixed(mixed); })
__extension__({ extern const char * reflect_first_name__Pair(main_Pair x) __asm__("_reflect::first_name(main_Pair)"); reflect_first_name__Pair(pair); })
__extension__({ extern const char * reflect_first_name__Mixed(main_Mixed x) __asm__("_reflect::first_name(main_Mixed)"); reflect_first_name__Mixed(mixed); })
extern void reflect_show__Pair(main_Pair * x) __asm__("_reflect::show(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]] void reflect_show__Pair(main_Pair * x) {
    printf("%zu fields:", (__SIZE_TYPE__) 2);
    {
        {
            printf(" %s=%lld", "m", (long long) x->m);
        }
    }
    {
        {
            printf(" %s=%g", "b", (double) x->b);
        }
    }
    printf("\n");
}
extern void reflect_show__Mixed(main_Mixed * x) __asm__("_reflect::show(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]] void reflect_show__Mixed(main_Mixed * x) {
    printf("%zu fields:", (__SIZE_TYPE__) 5);
    {
        {
            printf(" %s=?", "pair");
        }
    }
    {
        {
            printf(" %s=%lld", "bits", (long long) x->bits);
        }
    }
    {
        {
            printf(" %s=%lld", "more", (long long) x->more);
        }
    }
    {
        {
            printf(" %s=array(%zu)", "values", sizeof(x->values) / sizeof(x->values[0]));
        }
    }
    {
        {
            printf(" %s=%g", "scale", (double) x->scale);
        }
    }
    printf("\n");
}
extern double reflect_sum__Pair(main_Pair value) __asm__("_reflect::sum(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]] double reflect_sum__Pair(main_Pair value) {
    double total = 0;
    {
        {
            typeof(((main_Pair *) 0)->m) part = value.m;
            total += (double) part;
        }
    }
    {
        {
            typeof(((main_Pair *) 0)->b) part = value.b;
            total += (double) part;
        }
    }
    return total;
}
extern double reflect_sum__Mixed(main_Mixed value) __asm__("_reflect::sum(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]] double reflect_sum__Mixed(main_Mixed value) {
    double total = 0;
    {
    }
    {
        {
            typeof(U8) part = value.bits;
            total += (double) part;
        }
    }
    {
        {
            typeof(U8) part = value.more;
            total += (double) part;
        }
    }
    {
    }
    {
        {
            typeof(((main_Mixed *) 0)->scale) part = value.scale;
            total += (double) part;
        }
    }
    return total;
}
extern __SIZE_TYPE__ reflect_last_index__Mixed(void) __asm__("_reflect::last_index(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ reflect_last_index__Mixed(void) {
    return 5 - 1;
}
extern __SIZE_TYPE__ reflect_field_count__Pair(main_Pair x) __asm__("_reflect::field_count(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ reflect_field_count__Pair(main_Pair x) {
    return 2;
}
extern __SIZE_TYPE__ reflect_field_count__Mixed(main_Mixed x) __asm__("_reflect::field_count(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]] __SIZE_TYPE__ reflect_field_count__Mixed(main_Mixed x) {
    return 5;
}
extern const char * reflect_first_name__Pair(main_Pair x) __asm__("_reflect::first_name(main_Pair)");
[[gnu::weak]][[gnu::visibility("hidden")]] const char * reflect_first_name__Pair(main_Pair x) {
    return "m";
}
extern const char * reflect_first_name__Mixed(main_Mixed x) __asm__("_reflect::first_name(main_Mixed)");
[[gnu::weak]][[gnu::visibility("hidden")]] const char * reflect_first_name__Mixed(main_Mixed x) {
    return "pair";
}
