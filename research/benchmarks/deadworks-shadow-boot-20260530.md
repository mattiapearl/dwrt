# Deadworks-hosted DWRT Shadow Boot — 2026-05-30

Environment: local Deadlock dedicated server via temporary Deadworks hook chassis branch `experiment/dwrt-shadow-shim` in `C:\Code\deadworks-tournament`.

Profiler: xperf ETW via `scripts/profile-dwrt-command.ps1 -RequireProfiler`.

Raw ETW traces are intentionally not committed because ETW files can contain local paths/process data.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-deadworks-shadow-server.ps1 `
  -RequireProfiler `
  -TimeoutSeconds 30 `
  -DeadworksExe 'C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadworks_dwrt.exe'
```

The script sets:

```txt
DWRT_SHADOW_ENABLE=1
DWRT_RUNTIME_DLL=C:\Code\dwrt\target\release\dwrt_runtime.dll
DWRT_SHADOW_TIMING=1
DWRT_SHADOW_LOG_INTERVAL_MS=5000
DWRT_SHADOW_SLOW_THRESHOLD_NS=100000
DWRT_SHADOW_USERCMD_MOUNT_MASK=0
```

## Result

The server booted far enough to initialize Deadworks and load DWRT from inside the real server process:

```txt
[deadworks] [INF] PostInit
[deadworks] [INF] [DWRT] loaded runtime abi=1 path='C:\Code\dwrt\target\release\dwrt_runtime.dll' timing=1 qpcHz=10000000 slowThresholdTicks=1000 usercmdMountMask=0x0
[deadworks] [INF] StartupServer (map: dl_midtown)
```

The run was stopped by timeout after profiler capture. No gameplay behavior was changed.

## What this proves

- The real dedicated server process can load `dwrt_runtime.dll`.
- ABI version check succeeds in-process.
- DWRT runtime is created from the current hook chassis.
- The run can be captured under xperf ETW.

## What still needs a client

This boot-only run does not prove usercmd or net route calls because no client connected. The next in-game smoke needs a client connection to generate:

- `ProcessUsercmds` route calls,
- incoming net-message route calls,
- outgoing net-message route calls,
- shadow summaries with non-zero route counters.
