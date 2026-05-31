# Runtime testpoints and validation gates

Date: 2026-05-30

Purpose: keep every DWRT-native layer testable as we move from runtime ABI, to live-server load, to hook installation, to gameplay probes. These are explicit gates for catching drift, accidental Deadworks dependency, recursive hook logic, and hot-path regressions.

## Current testpoints in code

`dwrt_host.dll` snapshots include:

- `initializeCalls`
- `initializeReentrantRejects`
- `shutdownCalls`
- `callbackEntries`
- `callbackRecursiveEntries`
- `callbackCurrentDepth`
- `callbackMaxDepth`

`native/dwrt-host/dwrt_host_testpoints.hpp` provides `CallbackScope`, a future hook callback guard. Hook callbacks should create one scope at entry and immediately fall back to original/continue behavior if `recursive() == true` unless a hook is explicitly proven reentrant-safe.

## Gates that must stay green

### 1. Rust/runtime ABI

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-runtime.ps1
```

Expected:

- ABI version matches;
- net/usercmd routes work;
- unmounted native probes return no-interest before counter increments;
- mounted native probes increment counters and snapshot correctly.

### 2. Host resolver smoke

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck
```

Expected:

- `dwrt_host.dll` bootstrap smoke passes;
- native testpoints smoke proves nested `CallbackScope` increments recursive counters;
- file-backed signatures: required failures = `0`;
- mapped-module signatures: required failures = `0`;
- runtime probe ABI smoke passes.

### 3. Real live-server bootstrap

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1 `
  -WaitServerModuleSeconds 45 `
  -HoldSeconds 3 `
  -TimeoutSeconds 90
```

Expected host/injector gates:

- `initialized = true`
- `runtimeLoaded = true`
- `runtimeProbeOk = true`
- `signaturesChecked = true`
- `signatureRequiredFailures = 0`
- `usedLiveServerModule = true`
- `usedMappedFileFallback = false`
- `initializeCalls = 1`
- `initializeReentrantRejects = 0`
- `callbackRecursiveEntries = 0`
- profile metadata exit code = `0`

### 4. Hook-installed/no-interest gate

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1 `
  -InstallProbeHooks `
  -WaitServerModuleSeconds 45 `
  -HoldSeconds 3 `
  -TimeoutSeconds 90
```

Expected:

- server launches and loads live `server.dll`;
- hook install summary says all enabled hooks are installed or fail closed;
- current hook set: `TakeDamageOld`, `AcceptInput`, `FireOutputInternal`;
- `hookInstallAttempts = 3`;
- `hooksInstalled = 3`;
- `hookInstallFailures = 0`;
- per-hook callback count may be non-zero in longer runs;
- route/probe counted events remain zero when interests are unmounted;
- `callbackRecursiveEntries = 0`;
- max callback time stays under the agreed threshold once timing is added;
- no stdout/file writes from hook callbacks.

### 5. Future mounted probe gates

Damage probe:

- mounting damage increments damage counted events;
- unmounted entity I/O/touch counters remain zero;
- same-team/FFA tests record `mp_friendlyfire` cases separately.

Entity I/O probe:

- mounting input/output filters increments only matching filter counters;
- callback recursion counter remains zero;
- dropped-record counters remain zero for count-only runs.

### 6. Failure-injection gates

Run periodically before hook changes are considered safe:

- bad runtime path -> `DWRT_HOST_ERROR_RUNTIME_LOAD_FAILED` when runtime is required;
- bad/unknown `server.dll` -> signature required failures > 0 and no hook install;
- double initialize -> `DWRT_HOST_ERROR_ALREADY_INITIALIZED` and `initializeReentrantRejects` increments;
- recursive callback self-test/future induced recursion -> `callbackRecursiveEntries` increments and hook falls back to original behavior.

## Policy

A live gameplay probe is not valid unless the corresponding bootstrap and no-interest gates pass first. If a hook gate fails, do not continue into damage/entity/FFA measurements; fix the failing gate or mark the run invalid.
