extern char **environ;
extern __UINT32_TYPE__ foreign_counter;
extern __UINT32_TYPE__ foreign_triple(__UINT32_TYPE__ x);
__UINT32_TYPE__ sys_world_env_count(void) __asm__("_sys::world::env_count");
__UINT32_TYPE__ sys_world_env_count(void) {
__UINT32_TYPE__ count = 0;
for (char **entry = environ; *entry != nullptr; entry++)
count += 1;
return count;
}
__UINT32_TYPE__ sys_world_counter_times_three(void) __asm__("_sys::world::counter_times_three");
__UINT32_TYPE__ sys_world_counter_times_three(void) {
return foreign_triple(foreign_counter);
}
extern __UINT32_TYPE__ sys_world_owned __asm__("_sys::world::owned");
