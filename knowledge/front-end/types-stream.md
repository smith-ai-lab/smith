---
type: architecture
title: "The new front end: front::types_stream"
description: "front::types_stream (front/types_stream.smith, #1545): one area of smith's own front end (the map is knowledge/front-end.md)."
tags: [front-end]
---
# front::types_stream (`front/types_stream.smith`, #1545)

- A generator is a function whose written result is `stream::Stream(T)` and whose body has a `yield` outside any lambda; a stream-returning function with no `yield` is an ordinary function.
- `yield x` is valid only in a generator, and `x` converts to `T` by the ordinary conversion. Elsewhere (a lambda included) it is an error that says to return a stream or remove the yield.
- A generator ends with a plain `return`; `return v` is an error that says to `yield v` first. Its body is checked as the body of a function with no result.
- `stream::next(s)` on a local `stream::Stream(T)` has type `option(T)`; `stream::close(s)` closes it (#1723). The call rule lives in `types_expr` (`stream_callee`), because `types_stream` imports `types_expr`.
- No recursion: the search for a `yield` walks statements with a fixed stack. Hooked from `types_decl`; not wired into `smith cc`. Tests: `tests/front_types_stream_test.smith`.
