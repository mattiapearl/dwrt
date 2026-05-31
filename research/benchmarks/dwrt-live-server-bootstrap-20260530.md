# DWRT live-server bootstrap smoke

Date: 2026-05-30

Purpose: prove `dwrt_host.dll` can be loaded into a real running Deadlock dedicated-server process without Deadworks in the measured path, initialize `dwrt_runtime.dll`, resolve the live `server.dll` module, and emit validation artifacts.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1 `
  -WaitServerModuleSeconds 45 `
  -HoldSeconds 3 `
  -TimeoutSeconds 90
```

The script launches the real `deadlock.exe` with dedicated-server args, waits for live `server.dll`, injects `dwrt_host.dll`, calls `dwrt_host_initialize`, snapshots host state, then shuts the host down and terminates the child server process. It does not redirect the server console/stdout.

## Result

Run artifact:

- `research/benchmarks/runs/20260530-195400-dwrt-live-server/dwrt-live-host.json`
- `research/benchmarks/runs/20260530-195400-dwrt-live-server/dwrt-live-injector.json`
- `research/benchmarks/runs/20260530-195400-dwrt-live-server/profile/20260530-195411-dwrt-live-server.json`
- ETW: `research/benchmarks/runs/20260530-195400-dwrt-live-server/profile/20260530-195411-dwrt-live-server.etl` *(not committed)*

Summary gates passed:

- real child process launched from `deadlock.exe`;
- `server.dll` loaded live in that child process;
- `dwrt_host.dll` loaded into that child process;
- `dwrt_runtime.dll` loaded by host;
- runtime probe ABI smoke passed;
- required live `server.dll` signatures resolved with `0` failures;
- `usedLiveServerModule = true`;
- `usedMappedFileFallback = false`;
- `initializeCalls = 1`;
- `initializeReentrantRejects = 0`;
- `callbackRecursiveEntries = 0`.

Profiler metadata:

- profiler: `Xperf`
- preset: `Latency`
- timeout: false
- exit code: `0`

## Notes

This validates DWRT-native process load/bootstrap/signature resolution against a running server. It still does not install gameplay hooks or measure hook callback overhead. The next step is a DWRT-owned hook backend behind the resolved allowlist.
