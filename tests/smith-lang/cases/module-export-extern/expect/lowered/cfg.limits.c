extern __UINT32_TYPE__ cfg_limits_max_users __asm__("_cfg::limits::max_users");
__UINT32_TYPE__ cfg_limits_users_allowed() __asm__("_cfg::limits::users_allowed");
__UINT32_TYPE__ cfg_limits_users_allowed() { return cfg_limits_max_users * 2; }
