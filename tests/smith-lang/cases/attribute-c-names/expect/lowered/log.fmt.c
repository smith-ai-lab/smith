#include <stdio.h>
#include <stdarg.h>

[[gnu::format(printf, 1, 2)]] static int log_fmt_private_say(const char *format, ...) {
va_list args;
va_start(args, format);
int n = vprintf(format, args);
va_end(args);
return n;
}
[[gnu::format(printf, 1, 2)]] int log_fmt_say(const char *format, ...) __asm__("_log::fmt::say");
[[gnu::format(printf, 1, 2)]] int log_fmt_say(const char *format, ...) {
return log_fmt_private_say("%s", format);
}
__attribute__((format(printf, 1, 2))) int log_fmt_said(const char *format, ...) __asm__("_log::fmt::said");
__attribute__((format(printf, 1, 2))) int log_fmt_said(const char *format, ...) {
return log_fmt_private_say("%s", format);
}
[[gnu::format(scanf, 2, 3)]] static int log_fmt_private_scan(int fd, const char *format, ...);
