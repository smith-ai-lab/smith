extern __UINT32_TYPE__ counter_state_total __asm__("_counter::state::total");
__UINT32_TYPE__ counter_state_total __asm__("_counter::state::total") = 3;
extern __UINT32_TYPE__ counter_state_spare __asm__("_counter::state::spare");
__UINT32_TYPE__ counter_state_spare __asm__("_counter::state::spare");
extern const char *const counter_state_names[2] __asm__("_counter::state::names");
const char *const counter_state_names[2] __asm__("_counter::state::names") = {"low", "high"};
extern __UINT64_TYPE__ counter_state_chosen;
__UINT64_TYPE__ counter_state_chosen = 9;
[[maybe_unused]] constexpr __UINT32_TYPE__ counter_state_limit = 4;
static __UINT32_TYPE__ counter_state_hidden = 2;
__UINT32_TYPE__ counter_state_sum() __asm__("_counter::state::sum");
__UINT32_TYPE__ counter_state_sum() {
return counter_state_total + counter_state_spare + counter_state_limit + counter_state_hidden;
}
