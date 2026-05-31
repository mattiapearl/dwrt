# DWRT

DWRT is a Rust-native Deadworks-style server modding/runtime layer for the real Deadlock dedicated server.

It is **not** a full server rewrite and **not** an upstream Deadworks PR branch. The goal is a modular runtime that keeps the real server authoritative while exposing safe, interest-gated visitors, scoped hooks, versioned memory facts, and eventually Rust plugins/map scripts.

## Current status

Early architecture/prototype. Current crates are model/runtime foundations only:

- `dwrt-ffi` — stable C ABI structs/constants for C++/Rust interop.
- `dwrt-core` — allocation-free hot-path routing for net messages/usercmds.
- `dwrt-engine` — declarative map of real Deadlock server surfaces and exposure policy.
- `dwrt-entity` — typed non-Send controller/pawn/entity handles plus schema-backed field plans.
- `dwrt-hooks` — hook registry model with discovery facts, feature dependencies, frequency class, and shadow/active status.
- `dwrt-memory` — versioned memory/schema/signature/vtable fact manifests; no raw pointer access.
- `dwrt-runtime` — opaque runtime object plus exported C ABI for routing and count-only native probes.
- `dwrt-trace` — JSONL trace records, bounded trace buffers, and route-decision comparison helpers for shadow-mode validation.
- `native/dwrt-host` — DWRT-owned native signature resolver/host smoke for validating the real `server.dll` before hook installation.

## Design rules

- Visitors are read-only observation.
- Hooks are scoped mutation/blocking/call contexts.
- No-interest hot paths must return with no allocation and no parse.
- Engine handles are server-thread-only by default.
- Raw offsets/signatures stay behind versioned manifests.
- Deadlock/Source 2 remains authoritative for networking, auth, snapshots, simulation, physics, prediction, maps, resources, and protobuf serializers.

See:

- [`DWRT_PROJECT_BOUNDARY.md`](DWRT_PROJECT_BOUNDARY.md)
- [`DWRT_ENGINEERING_TODOS.md`](DWRT_ENGINEERING_TODOS.md)
- [`RUST_DEADWORKS_REWRITE_PLAN.md`](RUST_DEADWORKS_REWRITE_PLAN.md)
- [`research/server-book/`](research/server-book/)

## Validate

```bash
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
```

Windows/MSVC C ABI smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-runtime.ps1
```

DWRT-native host/signature smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck
```

DWRT live dedicated-server bootstrap smoke:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1
```

Runtime benchmark with profiler wrapper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1
```

Native stack profiling/debugging test suite:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-dwrt-native-stack.ps1
```

Add `-IncludeLiveServer -IncludeHookInstall` when you want the suite to validate real dedicated-server injection and DWRT-owned hook installation.

ETW profiling requires an elevated/admin shell. Use `-RequireProfiler` for runs where a missing profiler artifact should fail the run.
