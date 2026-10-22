// <smith/arena_typed.h>: the typed pushes of <smith/arena.h> for C and C++, which <smith/arena.h> includes. They are
// macros, which a .smith module cannot define, so they live here; a .smith file has arena::push and
// arena::push_array, with the same checks.
//   Foo *foo = SMITH_ARENA_PUSH(arena, Foo);
//   Foo *foos = SMITH_ARENA_PUSH_ARRAY(arena, Foo, count);
// They name smith_arena_push and smith_arena_push_array, which <smith/arena.h> declares. Include that, not this.
#pragma once

#define SMITH_ARENA_PUSH(arena, Type) ((Type *)smith_arena_push((arena), sizeof(Type), alignof(Type)))
#define SMITH_ARENA_PUSH_ARRAY(arena, Type, count) \
    ((Type *)smith_arena_push_array((arena), sizeof(Type), (count), alignof(Type)))
