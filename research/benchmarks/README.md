# DWRT Benchmarking and Profiling

DWRT benchmark runs should produce both a numeric report and, whenever possible, an ETW profiler artifact.

## Rule

When running in-game smoke tests or runtime benchmarks, profile the whole run:

```txt
start profiler -> run scenario -> stop profiler -> save metadata/report/logs
```

Do not optimize from route counters alone. Use profiler data to classify whether time is spent in DWRT routing, ABI calls, Deadworks/CoreCLR, protobuf parse/serialize, plugin callbacks, logging/IO, or normal server simulation.

## Local runtime benchmark

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1
```

This builds `dwrt-bench`, runs it, and writes a report under:

```txt
research/benchmarks/runs/<timestamp>/dwrt-bench.md
```

By default the script attempts ETW profiling with WPR/xperf. ETW CPU profiling requires an elevated/admin shell. In a non-admin shell the benchmark still runs and writes metadata that profiling was unavailable.

To require a profiler and fail if ETW cannot start:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1 -RequireProfiler
```

To disable profiling explicitly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1 -NoProfile
```

## Generic profiler wrapper

Use this for future in-game smoke commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/profile-dwrt-command.ps1 `
  -FilePath "cmd.exe" `
  -ArgumentLine "/c echo replace-with-server-smoke-command" `
  -Name "dwrt-ingame-smoke" `
  -RequireProfiler
```

Artifacts:

```txt
*.etl   # ETW trace, if profiler started
*.json  # command/profiler metadata
```

## Longframe classification

Every longframe report should classify likely cause as one of:

- DWRT shim/runtime routing overhead,
- Deadworks/CoreCLR/native-managed dispatch overhead,
- protobuf serialize/parse/mutation path,
- plugin callback time,
- logging/console/file IO,
- trace flushing/backpressure,
- server/game simulation or map load unrelated to mod layer,
- unknown / needs deeper instrumentation.
