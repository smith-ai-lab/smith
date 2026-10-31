---
name: smith-env
description: "Using smith::env (import smith::env; env::get, set_handler): reading an environment variable directly, absent as an option. Load when code imports smith::env or includes <smith/env.h>, or on reading HOME, PATH or any environment variable in .smith, or faking the environment in a test."
---

# The environment: smith::env

This skill ships in the smith plugin, from `.agents/skills/smith-env/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there. It uses `smith-option`.

`env::get(name)` reads a variable of the process. It returns `env::Value`, an `option` of the value's characters: `.none` when the variable is not set, `.some` of the text otherwise (an empty value is `.some` of an empty slice).

```smith
module where_home;

import smith::env;

export usize home_length(env::Value home) { // a function of its own reads an option (smith issue #353)
    if (home == .some) return countof(home.some);
    return 0;
}

export usize ask() {
    return home_length(env::get("HOME"));
}
```

- **The text belongs to the environment**: it stays valid until the variable changes and is never freed. Copy it to keep it.
- **The name is a slice**, not a terminated string: `env::get(name[0:4])` works. An empty name, one holding `=` or a NUL, or one longer than `env::name_max` (1023) is a bug: a checked failure, not `.none`.
- **The effect is `env`** (`smith-lang-effects`), inferred from the call.
- **Test with a fake**: `env::set_handler(&fake.base)` installs an `env::Handler` (C: `SmithEnvHandler {get}`, `smith_handlers_set_env`) for the thread and returns the previous one; restore it with `defer`. `getenv` of any other code on the thread reaches it too. `get(self, name)` returns the value or null. See `lib/smith/env_test.smith`.
- **From C**: `<smith/env.h>` has `smith_env_get(name)` (a slice) and `smith_env_set_handler`; `getenv` itself is faked by the handler.
- The program's arguments are `main(str args[:])`, not this module.
