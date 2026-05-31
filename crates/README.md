# dwrt Rust Runtime Crates

Minimal Rust-native runtime pieces for a Deadworks-style Deadlock server extension.

Current crates:

- `dwrt-ffi`: stable C ABI structs shared by a native hook shim and Rust runtime.
- `dwrt-bench`: benchmark binary for route/trace costs; intended to run under the profiler wrapper.
- `dwrt-core`: allocation-free hot-path routing primitives for net messages and usercmds.
- `dwrt-engine`: declarative map of real Deadlock server surfaces and safe default exposure policy.
- `dwrt-hooks`: hook boundary registry model with discovery facts, feature dependencies, frequency class, and shadow/active status.
- `dwrt-entity`: typed non-Send controller/pawn/entity handles plus manifest-backed schema field plans.
- `dwrt-memory`: versioned memory/schema/signature/vtable fact manifests; no raw pointer access.
- `dwrt-runtime`: opaque runtime object plus C ABI exports for routing and count-only native probes.
- `dwrt-trace`: JSONL trace records, bounded buffers, and route-decision comparison helpers for shadow-mode validation.

Run Rust tests:

```bash
cargo test --workspace
```

Run the C ABI smoke test on Windows/MSVC:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-runtime.ps1
```

Run the DWRT-native host/signature smoke test on Windows/MSVC:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck
```

Run the live dedicated-server bootstrap smoke:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1
```

Run profiled runtime benchmarks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1
```

Design rule: control paths may allocate and lock; net/usercmd hot hooks must be able to answer no-interest/no-work through atomics and return immediately.
