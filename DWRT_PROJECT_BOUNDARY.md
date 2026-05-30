# DWRT Project Boundary

Date: 2026-05-29

## Decision

DWRT is an entirely new Deadworks-style rework, not a PR branch and not an incremental upstream Deadworks feature.

The prior Deadworks PR work remains separate. DWRT should be developed as its own runtime architecture with its own crates, ABI, hook registry, memory manifest, plugin model, and eventual native shim.

## What DWRT is

DWRT is a Rust-native server modding/runtime layer for the real Deadlock dedicated server:

```txt
Deadlock dedicated server executable
  -> minimal native hook/bootstrap shim
  -> DWRT Rust runtime
  -> Rust plugins and/or map scripts
```

It should reuse lessons/evidence from Deadworks, but it is not constrained by Deadworks' public API compatibility, C#/.NET bridge, or upstream review tolerance.

## What remains separate

- Deadworks upstream PR branches.
- Deadworks C# plugin API compatibility work.
- Tournament/scrim product policy.
- Research-only probes that are not ready to become public runtime concepts.

## Design implications

- We can redesign APIs around visitors, hooks, commands, manifests, and typed facades.
- We do not need to preserve C# compatibility.
- We should keep the C++ layer minimal and disposable.
- We should keep the Rust crates modular and tested.
- We should prefer shadow-mode in-game validation before behavior-changing hooks.
- We should not expose raw pointer/offset access just because Deadworks probes used it.

## Repository hygiene

DWRT work should be easy to separate into its own repository or dedicated branch later:

```txt
Cargo.toml
Cargo.lock
crates/dwrt-*
DWRT_*.md
RUST_DEADWORKS_REWRITE_PLAN.md
research/server-book/*
```

Avoid mixing DWRT implementation with existing scrim plugin/product code unless explicitly integrating later.
