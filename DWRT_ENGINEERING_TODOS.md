# DWRT Engineering TODOs

Date: 2026-05-29

These are architectural guardrails for the Rust-native Deadworks rewrite. DWRT is a separate rework, not a Deadworks PR branch. Treat these as acceptance criteria before exposing public APIs.

## Core vocabulary

- **Visitor**: read-only observation. Visitors may receive compact snapshots/views and must not mutate engine state.
- **Hook**: scoped intervention point. Hooks may block, mutate, or call engine functions only through an explicit context and capability.
- **Command**: controlled call into the engine from runtime/plugin/script code.
- **Manifest fact**: versioned schema/signature/offset/vtable/interface fact used internally to build safe wrappers.

## TODO: module hygiene

- [ ] Keep crate roots (`lib.rs`) as re-export/navigation files only.
- [ ] Split types, errors, manifests, runtime exports, and tests into separate modules.
- [ ] Prefer files under ~300-500 lines unless they are isolated data tables or generated code.
- [ ] Move large tests into `*_tests.rs` modules.
- [ ] Keep public type names stable and re-exported from crate roots.
- [ ] Do not mix hook installation, routing decisions, plugin dispatch, and memory facts in the same module.

## TODO: separate reads from writes

- [ ] Define all read-only APIs as `Visitor` APIs:
  - [ ] `UsercmdVisitor`
  - [ ] `NetMessageVisitor`
  - [ ] `EntityVisitor`
  - [ ] `GameEventVisitor`
  - [ ] `Snapshot/NetworkTraceVisitor`
- [ ] Ensure visitors receive snapshots/views, not raw mutable pointers.
- [ ] Add tests that visitor contexts do not expose mutation methods.
- [ ] Define all mutation/blocking APIs as `Hook` APIs:
  - [ ] `UsercmdHook` for full protobuf/usercmd mutation.
  - [ ] `NetMessageHook` for block/mutate/recipient changes.
  - [ ] `EntityIoHook` for input/output blocking.
  - [ ] `GameEventHook` only where engine semantics support blocking/modification.
- [ ] Hooks must have scoped commit semantics:
  - [ ] no mutation after context drop,
  - [ ] explicit result: continue / handled / stop,
  - [ ] mutation written back only if marked dirty.
- [ ] Do not expose arbitrary `read(ptr + offset)` / `write(ptr + offset)` APIs to plugins.

## TODO: threading model

- [ ] Declare the server thread as the only thread allowed to call engine functions unless a function is proven thread-safe.
- [ ] Make engine handles `!Send` by default.
- [ ] Allow background workers to process owned snapshots only.
- [ ] Add a server-thread task queue for background workers to request engine operations.
- [ ] Use atomics or prebuilt immutable tables for hot-path interest checks.
- [ ] Avoid locks in net/usercmd hook hot paths.
- [ ] Add tests for concurrent interest add/remove while routing reads occur.
- [ ] Add debug assertions for wrong-thread engine access in dev builds.

## TODO: internal communication buffering

- [ ] Add bounded MPSC queues for runtime-internal events.
- [ ] Define backpressure policy per event class:
  - [ ] hot telemetry may drop newest/oldest with counters,
  - [ ] command/admin events must not silently drop,
  - [ ] mutation hooks must stay synchronous/scoped.
- [ ] Add preallocated per-thread scratch buffers for protobuf serialization/mutation paths.
- [ ] Add ring-buffered diagnostics instead of direct hot-path logging.
- [ ] Add counters for dropped events, queue depth high-water marks, slow callbacks, and dirty writebacks.
- [ ] Ensure trace/JSONL output is buffered and rate-limited.

## TODO: adaptable hook system

- [ ] Create a hook registry:
  - [ ] hook name,
  - [ ] module,
  - [ ] discovery method,
  - [ ] signature/vtable/interface fact keys,
  - [ ] expected ABI,
  - [ ] frequency class,
  - [ ] feature flags using the hook.
- [ ] Support hook capability negotiation at runtime.
- [ ] Support shadow mode where Rust routes decisions without changing engine behavior.
- [ ] Support per-hook health status:
  - [ ] resolved,
  - [ ] installed,
  - [ ] disabled by missing fact,
  - [ ] disabled by failed validation.
- [ ] Add versioned memory manifest validation before installing hooks.
- [ ] Add fallback behavior when a hook cannot resolve.
- [ ] Keep hook installation separate from plugin registration.

## TODO: visitor/hook API shape

Preferred naming:

```rust
ctx.usercmd().visit(fields, on_usercmd_readonly);
ctx.usercmd().hook_full(on_usercmd_mutation);

ctx.net().visit_incoming(msg_id, on_net_readonly);
ctx.net().hook_outgoing::<T>(on_net_mutation);

ctx.entities().visit_spawned(class_filter, on_entity_readonly);
ctx.entities().hook_input(class, input, on_entity_input);
```

Rules:

- [ ] `visit_*` APIs are read-only and cheap.
- [ ] `hook_*` APIs are potentially mutating/blocking and must be capability-gated.
- [ ] Visitor subscriptions should be composable and ref-counted.
- [ ] Hook subscriptions should define ordering and conflict behavior.
- [ ] Visitor dispatch should be optionally buffered when not needed synchronously.
- [ ] Hook dispatch must remain synchronous when engine behavior depends on the return value.

## TODO: tests required before live integration

- [ ] No-interest route tests for every hot path.
- [ ] Visitor read-only compile/API tests.
- [ ] Hook dirty-writeback tests.
- [ ] Interest ref-count tests.
- [ ] Concurrent registration/routing tests.
- [ ] Bounded queue overflow tests.
- [ ] Hook registry validation tests.
- [ ] Memory manifest confidence/access tests.
- [ ] Shadow-mode comparison tests against current Deadworks routing.
