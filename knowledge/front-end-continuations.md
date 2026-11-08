# ThenGeneral as a payload enum (design notes for #2650, D23)

`state::ThenGeneral` (front/emit_state.smith) is one struct that every continuation not yet given its own shape fills
differently: `run` plus whichever of `branch expr stmt decl hint ty number flag blocks value span` the continuation
reads. Count on main: ~146 uses in 15 files (emit_memory 20, emit_match 19, emit_option 18, emit_control 16, emit 13,
emit_tuple 10, emit_iter 9, emit_call 6, emit_builtin 6, emit_lambda 6, emit_state 7, emit_value 7, emit_stream 5,
emit_arith 2, emit_vector 2). D23: a struct whose constructions fill different subsets is an enum pretending to be a
struct, so each state gets a variant with only the fields it reads; no zero fill, no `= undefined`.

## Shapes found at the construction sites (`ThenGeneral){` in front/emit*.smith)

Every shape also has `span` (all of them) and its own `run` (a function taking `(Emitter *, const <Variant> *)`).

| Shape (variant) | Fields | Continuations |
|---|---|---|
| `Span` only | span | value_after_end (emit), next_finish, close_finish (emit_stream), try_branch (emit_option) |
| `Expr` | expr | global_place (emit_memory), array_literal_finish (also the shape count), tuple/record finishes |
| `ExprCount` | expr, number (a count/rank) | builtin finish, alloc_finish, construct_finish, call_finish (emit_call), record_finish (emit_tuple, record index) |
| `ExprCountTy` | expr, number, ty | lambda call_finish (func_ty), compound_step with a slot (value too) |
| `ExprFlag` | expr, flag | member_finish (arrow) |
| `ExprTy` | expr, ty | filled_finish (array) |
| `ExprHint` | expr, hint | lambda_argument (emit_call) |
| `Ty` | ty | some_finish, literal_finish (emit_tuple), array_slice_finish, build_finish with a count |
| `TyFlag` | ty, flag | float_cast_finish, cast_finish (checked), result_finish (error), declare_finish also decl |
| `TyNumber` | ty, number | variant_finish (emit_match), vector build_finish |
| `Index` | number | tuple member_finish, place_finish |
| `Stmt` | stmt | let_branch, if_let_branch, destructure_finish, range_start, static_assert_finish (stmt form) |
| `StmtHint` | stmt, hint | value_start (emit, emit_lambda), dispatch (emit_match, plus flag is_value) |
| `StmtFlag` | stmt, flag | value_start of a `for` by reference |
| `Decl` | decl | static_assert_finish (decl form), declare_finish (+ty, +flag) |
| `Return` | value, ty? | ret_value (emit, emit_lambda), ret_none (emit_option) |

Fields never read together are the point: `blocks[3]` and `branch` are read by only a few (if/match/loop closers) and
belong to `ThenBlocks`, which is already its own struct; keep them off the enum except where a variant needs a branch
callback (the `if let` used as a value).

## Plan

1. Add `Then.<variant>` per row above; `state::push_then_general(...)` becomes one `push_then(Emitter *, Then)`.
2. `Continuation` is replaced by one run type per variant (`typedef Status (*ExprCountRun)(Emitter *, const ExprCount *)`).
3. Convert file by file; a sed-able mechanical step: the `.run = f, .expr = x, .number = n, .span = s` literals map 1:1
   to variants. The compiler lists what still uses `ThenGeneral` until it is empty, then delete it (and `Plan` /
   `ThenBlocks` constructions that differ get the same treatment).
4. Run `front_emit_*`, `front_e2e*`, `front_builtin_*`; a stale field read becomes a compile error, which is the gain.

Order to land: after the open emitter PRs (#2795, #2802) so there is no rebase churn in emit_memory/emit_control.
