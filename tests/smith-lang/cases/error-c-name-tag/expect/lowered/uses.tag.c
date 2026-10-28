#include <sys/stat.h>

int uses_tag_size_bad() __asm__("_uses::tag::size_bad");
int uses_tag_size_bad() {
struct stat info;
return (int)sizeof info;
}
int uses_tag_size_ok() __asm__("_uses::tag::size_ok");
int uses_tag_size_ok() {
struct stat info;
return (int)sizeof info;
}
