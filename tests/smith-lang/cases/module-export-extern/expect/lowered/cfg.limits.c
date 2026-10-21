extern __UINT32_TYPE__ cfg_limits_max_users __asm__("_cfg::limits::max_users");
__UINT32_TYPE__ cfg_limits_users_allowed(void) __asm__("_cfg::limits::users_allowed");
__UINT32_TYPE__ cfg_limits_users_allowed(void) { return cfg_limits_max_users * 2; }
