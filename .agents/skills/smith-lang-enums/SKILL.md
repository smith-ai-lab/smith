---
name: smith-lang-enums
description: "Enums with payloads in .smith: `typedef enum { Circle circle; Rect rect; empty; } Shape;`, picking a variant with a leading dot (`.empty`, `{.circle = {.radius = 1}}`), `s == .circle`, `switch` with no default, and reading `s.circle` only where the tag was tested (narrowing). Load when a .smith file declares an enum with a type and a name inside, or on `smith-enum:`, 'needs a type to look in', 'carries a payload', 'has no variant', 'is read where ... may not be a'."
---

# Enums with payloads

C's tagged union (a tag beside a union) lets you read a member the tag does not say is there. In a `.smith` file with a `module` line an enum can carry a payload, and the compiler checks the read. Plain C enums (`enum { A, B }`) keep C's meaning.

```smith
typedef struct { F32 radius; } Circle;
typedef struct { F32 w; F32 h; } Rect;
typedef enum {
    Circle circle;        // a type and a name, as a union member
    Rect rect;
    struct { I32 a; } one; // an anonymous struct for a one-off payload
    empty;                // no payload: a name
} Shape;

Shape make(Bool big) {
    if (big) return {.rect = {.w = 2, .h = 3}};   // a payload: a designated initializer
    return .empty;                                 // no payload: a leading dot
}
Bool is_big_circle(Shape s) { return s == .circle && s.circle.radius > 1.0f; }
F32 area(Shape s) {
    switch (s) {                                   // every variant named, no default:
    case .circle: return s.circle.radius * s.circle.radius * 3;
    case .rect: return s.rect.w * s.rect.h;
    case .one: return (F32)s.one.a;
    case .empty: return 0;
    }
    return -1;
}
void grow(Shape *p) {
    switch (*p) { case .circle: p->circle.radius *= 2; break; case .rect: case .one: case .empty: break; }
}
```

## Rules

- **The dot** (`.name`) is looked up in the type the context gives: a declaration or assignment, `return`, an argument, the other side of `==` or `!=`, the value of a `switch`. Anywhere else: ``error: `.empty` needs a type to look in: write `Shape x = .empty;` ``. A variant the type lacks is ``` `Shape` has no variant `nothing` ```; a payload variant used alone is ``` `.circle` carries a payload: write `{.circle = ...}` ```.
- **`switch`** over a payload enum names every variant (`-Wswitch-enum`, an error even with a `default:`); `default:` is allowed, narrows nothing, and idiomatic Smith writes none (`smith-idiom`).
- **Narrowing.** Reading `s.circle` or `p->circle` (writing into it too) is an error unless on every path `s == .circle` held, by an `if`, `&&`, `||`, `!`, `?:`, a loop condition, an early return, or a `case .circle:`. An assignment to `s` (`s = .empty;`), `&s`, or a call that is given `p` ends it. The error: ``smith-enum: 's.circle' is read where 's' may not be a circle; test 's == .circle' or switch on 's'``. Writing a whole value (`*p = other;`) is how a tag changes; there is no write of the tag alone.
- **No binding syntax:** the payload is the union member, `s.circle.radius`, read where narrowed.
- **From C** the type is a struct with `tag` (an enum `<Type>_tag`, constants `<Type>_tag_<variant>`) and an unnamed union, unchecked, in the module's generated header; the check is smith's, in the compiler and `smith lsp`, not in `smith lint`.
- **In a generic** a function whose result is a payload enum may `return .none;` and `return {.some = i};` (written per instance); any other `.name` in a generic's body is an error saying so (known gap): compare and assign with the constants there.
- **`Option(T)` and `Result(T, E)` are built in**: in scope in every module with no import, `Option(Usize) find(...)` returning `i` (converted to `.some`) or `.none`, `Result(F64, I32)` returning `v` (converted to `.ok`) or `{.error = e}` (skills `smith-option`, `smith-result`). A module cannot declare its own (an error names the built-in); the rules below are for a type function you write under another name.
- **A type function that returns an enum** gives a generic-looking enum: `Type maybe(constexpr Type T) { return enum { T some; none; }; }`, used as `maybe(Usize) find(...)` and `maybe(Usize) x = {.some = 1};`; equal arguments are one struct, also across modules (an export's header defines it, and two modules or an importer that write `maybe(Usize)` share it). A generic may return `maybe(Usize)`. For now (each an error saying "known gap"): the body is exactly `return enum {...};` with `type` parameters, the arguments are written type names; a generic may also write `maybe(T)` (`constexpr Type T`) and `maybe(typeof(x))` (`auto x`) in its head and body, and each instance gets the struct a written `maybe(Usize)` is (an exported generic needs `export Type maybe(...)`); the first mention of `maybe(Usize)` may be anywhere a type is written, inside a function body too (its struct is defined in the block, the same type as the file-scope one); a module may `export Type maybe(constexpr Type T) {...}`: an importer writes `mod::maybe(X)` for any X and gets the same struct as the module's own `maybe(X)` (the header carries the shape); two modules that write the same `Type maybe` unexported each define theirs.

Overview: `smith-lang`. Reference: [knowledge/smith-language.md](../../../knowledge/smith-language.md), [knowledge/affine-types.md](../../../knowledge/affine-types.md) (the plugin).
