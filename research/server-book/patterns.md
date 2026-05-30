# Engine / Modding Patterns To Compare

Use this checklist when a data-handling or controller-abstraction decision is unclear. Do not copy architecture blindly; extract the small proven pattern.

## Native mod loaders

- **Metamod:Source / SourceMod**
  - Central plugin registry and forwards/callbacks.
  - Native extension boundaries are explicit.
  - Good reference for capability/version negotiation and plugin unload discipline.

- **GoldSrc / Quake-style usercmd loops**
  - User commands are sequence-numbered, validated, then consumed by movement/prediction.
  - Good reference for deterministic command history and stale/duplicate handling.

## Rust/server patterns

- **Bevy / ECS schedules**
  - Explicit phases and systems are useful for map scripts later.
  - Do not put ECS in the hot hook path until proven necessary.

- **OpenRA command/order model**
  - Deterministic command stream and replay validation.
  - Good reference for golden trace/replay tests.

- **mlua / Rhai / WASM plugin models**
  - Useful for script sandboxing and map rules.
  - Keep native Rust plugins separate from untrusted scripts.

## Rules adopted for dwrt

1. Hot-path registries use fixed-size atomics or prebuilt tables.
2. Control-plane registration can allocate/lock; packet/usercmd hooks cannot.
3. Plugin-facing handles are semantic wrappers, not raw pointers.
4. Versioned memory manifests own offsets/signatures.
5. Every runtime abstraction needs a trace/replay test before it becomes public API.
