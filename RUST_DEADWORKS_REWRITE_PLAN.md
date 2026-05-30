# Rust Deadworks Rewrite Plan

Date: 2026-05-29

## Project boundary

DWRT is an entirely new Deadworks-style rework, not an upstream PR branch and not an incremental Deadworks feature. See [`DWRT_PROJECT_BOUNDARY.md`](DWRT_PROJECT_BOUNDARY.md).

## Corrected goal

We are **not** rebuilding the Deadlock/Source 2 server from scratch.

The goal is to keep using the real Deadlock dedicated server executable and build a minimal, optimized, extensible server-modding layer on top of it: a Deadworks-style rewrite with Rust as the primary runtime instead of C#.

Working description:

> A native Rust server extension layer for Deadlock dedicated servers: lower overhead than Deadworks, explicit interest/mount systems, safe-ish curated memory access, first-class map/game scripts, and a stable plugin ABI.

## Non-goals

- Do not implement a complete Source 2 server.
- Do not emulate Steam networking/client auth/snapshots as a first milestone.
- Do not expose arbitrary memory read/write APIs to plugins by default.
- Do not make scrim/tournament policy the core public API; keep that as plugins/scripts.
- Do not depend on C#/.NET/CoreCLR in the hot path.

## Relationship to Deadworks

Deadworks is the current reference implementation for:

- hook points,
- signatures and memory data loading,
- lifecycle events,
- entity/schema access patterns,
- net-message interception,
- usercmd interception,
- game event dispatch,
- command/chat plumbing,
- plugin ergonomics.

The Rust rewrite should preserve the good public concepts while avoiding Deadworks' expensive defaults:

- no unconditional native->managed calls,
- no unconditional protobuf serialization/parsing,
- no hot-reload/watchers in production by default,
- no high-frequency logging by default,
- no large dynamic reflection path in hot callbacks.

## Engineering guardrails

See [`DWRT_ENGINEERING_TODOS.md`](DWRT_ENGINEERING_TODOS.md) for tracked TODOs around read/write separation, threading, buffering, adaptable hook registration, and the rule that **visitors are for read-only observation while hooks are for blocking/mutation/calls**.

## Proposed architecture

### 1. Loader / bootstrap

A tiny native bootstrap is still useful because Source 2 SDK headers and existing hook libraries are C++-friendly.

Options:

1. **Hybrid C++ shim + Rust core**
   - C++ does DLL/exe attachment, interface resolution, and low-level hook install.
   - Rust owns runtime state, routing, plugin ABI, config, scripting, diagnostics.
   - Lowest risk and fastest path because it can reuse Deadworks' proven hook chassis.

2. **Mostly Rust native module**
   - Rust uses `windows`, `retour`/custom detours, and raw FFI for engine interfaces.
   - Cleaner long-term, but higher risk for Source 2 C++ ABI/vtable details.

Recommendation: start hybrid, aggressively minimize C++ to a stable FFI boundary.

### 2. Rust core crates

```txt
crates/dwrt-core
  lifecycle, config, logging, scheduler, runtime state

crates/dwrt-ffi
  C ABI structs/functions exported to the C++ shim

crates/dwrt-hooks
  typed hook event models and interest/mount registries

crates/dwrt-memory
  signature database, module scanner wrappers, curated offsets, schema fields

crates/dwrt-entity
  entity handles, schema accessors, typed wrappers, lifetime checks

crates/dwrt-net
  net message ids, interest gates, protobuf fast paths, mutation path

crates/dwrt-usercmd
  compact usercmd visitors, button triggers, full protobuf compatibility if mounted

crates/dwrt-script
  map scripts / game mode scripts, likely Lua/Rhai/WASM after core stabilizes

crates/dwrt-plugin-api
  stable plugin-facing Rust API

crates/dwrt-plugin-loader
  dynamic plugin loading, version negotiation, permissions/capabilities
```

### 3. Runtime model

Everything high-frequency is mounted/interest-gated:

```txt
No interest => no parse, no allocation, no plugin callback.
Fast interest => curated native/Rust field extraction only.
Full interest => protobuf parse/mutation path only for exact ids/features.
```

Core event classes:

```txt
Lifecycle: startup, map start/end, shutdown
Clients: connect, put-in-server, full-connect, disconnect
Game frame: optional, tick-gated, off by default for plugins
Entities: create/spawn/delete/touch/input/output, mounted by class/event
Game events: named event subscriptions
Net messages: incoming/outgoing/user-message nested interest gates
Usercmd: count, fast fields, triggers, full protobuf if explicitly mounted
Commands/chat: command registry, permission policy, reply helpers
Timers/tasks: tick timers, map timers, async low-priority jobs
```

### 4. Plugin model

Prefer Rust-native plugins first:

```rust
#[dwrt_plugin]
fn init(ctx: &mut PluginContext) {
    ctx.commands().chat("heal", heal_handler);
    ctx.net().visit_user_message(CHAT_MSG, on_chat_user_message);
    ctx.usercmd().visit(UsercmdFields::BUTTONS | UsercmdFields::VIEW_ANGLES, on_cmd);
}
```

Plugin loading options:

1. `cdylib` Rust plugins with a stable C ABI registration function.
2. WASM plugins for sandboxed scripts later.
3. Lua/Rhai map scripts for server operators and map-specific rules.

Recommended layering:

```txt
Rust native plugins: powerful, trusted, compiled.
Map scripts: constrained, hot-loadable, lower privilege.
Config rules: declarative permissions/game mode policy.
```

### 5. Memory hooking philosophy

Memory access should be curated and capability-based:

- Engine interfaces first.
- Schema fields second.
- Signature-resolved functions third.
- Hard offsets only behind versioned memory manifests.
- Arbitrary pointer/offset reads are research-only, not public plugin API.

Expose handles/wrappers, not pointers:

```rust
PlayerController
PlayerPawn
Entity
GameRules
TraceSystem
RecipientFilter
NetMessageContext
UsercmdView
```

Each wrapper should encode validity checks and server-thread assumptions.

## What to reuse from Deadworks immediately

- Hook locations and signatures.
- Memory data loader shape.
- Usercmd hook and compact field extraction.
- Net-message hook locations and interest-gate design.
- Entity/schema access patterns.
- DiagnosticsPlugin concepts, but rewritten as Rust examples/tests.
- Native capability/version negotiation concept.

## First implementation milestone: Rust runtime under current hook chassis

Goal: prove Rust can replace CoreCLR dispatch for hot paths.

### Scope

- Keep a small C++ shim in the Deadworks fork.
- Load a Rust `dwrt_core.dll` or link Rust staticlib into native binary.
- Export C ABI callbacks from Rust:

```c
void dwrt_initialize(const DwrtHostApi* host);
void dwrt_on_startup_server(const char* map_name);
void dwrt_on_game_frame(uint8_t simulating, uint8_t first, uint8_t last);
void dwrt_on_fast_usercmds(int slot, const FastUsercmdNative* cmds, int count, uint8_t paused, float margin);
void dwrt_on_fast_net_message(const FastNetMessageNative* msg);
```

- Implement Rust interest registries for net/usercmd.
- Add one built-in Rust example plugin/module:
  - log load/unload,
  - chat command,
  - fast usercmd every N batches,
  - fast net pause/user-message visitor.

### Acceptance

- Server boots with Rust runtime.
- No plugin/no-interest baseline has near-zero extra work.
- Fast usercmd visitor works without protobuf parse.
- Fast net visitor works without protobuf serialization.
- Basic command/chat dispatch works.
- No .NET runtime required.

## Second milestone: Plugin ABI

- Define plugin manifest/version/capabilities.
- Dynamic Rust plugin load/unload in dev mode.
- Production mode loads once, no watcher.
- Subscription handles with deterministic cancel/drop semantics.
- Plugin permissions:
  - commands,
  - memory wrappers,
  - net mutation,
  - usercmd full protobuf,
  - entity mutation.

## Third milestone: Map scripts

Script layer should be lower privilege than native Rust plugins.

Candidate runtimes:

- Lua via `mlua`: familiar, embeddable, good for map scripts.
- Rhai: Rust-native, simpler sandboxing, less familiar.
- WASM: strongest sandbox story, more implementation overhead.

Initial script API:

```lua
on_map_start(function(ctx)
  ctx.chat.broadcast("custom rules loaded")
end)

command("ready", function(player, args)
  state.ready[player:slot()] = true
end)

on_player_death(function(ev)
  -- map/game mode rules
end)
```

## Research tasks

### A. Hook inventory

Build a table from Deadworks + IDA:

```txt
Name | Module | Signature/source | Function address strategy | Arguments | Frequency | Rust event type | Hot-path policy
```

Start with:

- `ProcessUsercmds`
- `CServerSideClientBase::FilterMessage`
- `IGameEventSystem::PostEventAbstract`
- game events
- entity created/spawned/deleted
- touch/input/output
- client connect/disconnect/full-connect
- game frame

### B. Memory manifest

Create versioned facts:

```txt
Deadlock build id / exe hash
CUserCmd stride
CUserCmd protobuf offset
CCitadelUserCmdPB fast-field offsets
Controller command queue/history offsets
Vtable indices for known calls
Schema offsets used by wrappers
```

### C. Rust FFI ABI

Define packed/stable structs for hot callbacks:

```rust
#[repr(C)]
pub struct FastUsercmdNative { ... }

#[repr(C)]
pub struct FastNetMessageNative { ... }
```

Never pass Rust-owned allocations across C++ unless ownership is explicit.

### D. Golden traces

Before replacing more code, emit JSONL traces from current Deadworks and assert Rust runtime produces equivalent routing/decisions.

## Initial implementation slice (2026-05-29)

Created a minimal Rust workspace:

```txt
crates/dwrt-ffi                    # stable C ABI structs/constants
crates/dwrt-core                   # interest-gated net/usercmd routing primitives
crates/dwrt-engine                 # declarative real-server surface catalog
crates/dwrt-memory                 # versioned memory/schema/signature fact manifests
crates/dwrt-entity                 # typed controller/pawn handles and schema-backed field plans
crates/dwrt-hooks                  # hook boundary registry, discovery requirements, shadow/active status
crates/dwrt-runtime                # opaque runtime object + exported C ABI functions
crates/dwrt-runtime/include/*.h    # tiny C shim header
crates/dwrt-trace                  # JSONL traces and route-decision comparison helpers
```

Validated:

```txt
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cargo build -p dwrt-runtime --release
C header smoke compile/link/run with MSVC against dwrt_runtime.dll.lib
```

Current exported runtime DLL symbols include:

```txt
dwrt_abi_version
dwrt_runtime_new / dwrt_runtime_free
dwrt_net_add_fast / dwrt_net_remove_fast
dwrt_net_add_serialized / dwrt_net_remove_serialized
dwrt_net_add_user_fast / dwrt_net_add_user_serialized
dwrt_net_route
dwrt_usercmd_set_mount_mask / dwrt_usercmd_route
```

## Immediate next actions

1. Add a Deadworks-side C++ shim experiment that loads/calls `dwrt_runtime.dll` without changing hook behavior.
2. Route one local hot path through Rust in shadow mode and compare Rust decisions against current C++/C# routing.
3. Add golden JSONL traces for net/usercmd decisions.
4. Use IDA only for missing hook/controller facts that runtime traces cannot answer.
