# DWRT Native Shim Experiments

This directory contains the minimal native-side code used to prove DWRT can be loaded, called from C++, and resolved against the real `server.dll` before we install real game hooks.

## `dwrt-shim`

`native/dwrt-shim` dynamically loads `dwrt_runtime.dll`, resolves the exported C ABI, owns an opaque `DwrtRuntime`, and records shadow route/timing counters.

It does **not** hook the game yet and does **not** change engine behavior. It is the C++ side of the MVP vertical slice before wiring into real Deadlock/Deadworks hook points.

Run the smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-shim.ps1 -RequireProfiler
```

The smoke test is intended to run under the profiler wrapper so native load/call overhead can be captured before in-game integration.

## `dwrt-host`

`native/dwrt-host` is the DWRT-owned probe foundation for the real server path. It currently provides:

- `dwrt_host.dll`, a minimal bootstrap DLL with explicit `dwrt_host_initialize` / `dwrt_host_shutdown` exports and a tiny `DllMain` that does no heavy work;
- PE32+ `server.dll` file loading and section/RVA mapping;
- mapped-image/module scanning for the same RVA resolver mode needed in an in-process host;
- live `GetModuleHandleW(L"server.dll")` resolver support in `dwrt_host.dll`, with mapped-file fallback for smoke tests;
- byte-pattern signatures with `?`/`??` wildcards;
- a manifest of evidence-backed function signatures for damage, entity I/O, entity creation, usercmd, ability, and related boundaries;
- a host smoke executable that loads `dwrt_runtime.dll`, exercises the count-only probe C ABI, scans `server.dll`, and fails closed when required signatures are missing, non-unique, or drift from expected RVAs;
- an explicitly gated walker patrol write experiment that can apply velocity-only `Teleport` calls to damaged `npc_boss_tier2` / `alt_npc_boss_tier2` entities.

The current hook installer is allowlisted and experimental. Write experiments must stay behind explicit flags and artifact counters.

Run the host smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck
```

For overhead runs, omit `-NoProfile` and use `-RequireProfiler` when ETW capture is mandatory.

Run the live dedicated-server bootstrap smoke:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1 `
  -WaitServerModuleSeconds 45 `
  -HoldSeconds 3 `
  -TimeoutSeconds 90
```

This launches the real `deadlock.exe`, waits for live `server.dll`, injects `dwrt_host.dll`, calls explicit initialization, verifies live-module resolution/testpoints, then shuts the child process down. It does not redirect the server console/stdout.

Add `-InstallProbeHooks` to validate the current allowlisted no-interest hook install path (`TakeDamageOld`, `AcceptInput`, `FireOutputInternal`).

Manual walker velocity write experiment:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/start-dwrt-manual-probe-session.ps1 `
  -Detached `
  -NoProfile `
  -WalkerPatrolExperiment `
  -WalkerPatrolStride 16
```

This does not rewrite AI schedules. It applies a velocity-only `CBaseEntity::Teleport` call from the server-thread damage hook when a damaged victim has designer name `npc_boss_tier2` or `alt_npc_boss_tier2`. Use it only in disposable local sessions.
