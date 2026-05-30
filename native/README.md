# DWRT Native Shim Experiments

This directory contains the minimal native-side code used to prove DWRT can be loaded and called from C++ before we install real game hooks.

## `dwrt-shim`

`native/dwrt-shim` dynamically loads `dwrt_runtime.dll`, resolves the exported C ABI, owns an opaque `DwrtRuntime`, and records shadow route/timing counters.

It does **not** hook the game yet and does **not** change engine behavior. It is the C++ side of the MVP vertical slice before wiring into real Deadlock/Deadworks hook points.

Run the smoke test:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-shim.ps1 -RequireProfiler
```

The smoke test is intended to run under the profiler wrapper so native load/call overhead can be captured before in-game integration.
