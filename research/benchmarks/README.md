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

By default the script attempts ETW profiling with xperf/WPR. ETW CPU profiling requires an elevated/admin shell. In a non-admin shell the benchmark still runs and writes metadata that profiling was unavailable. When both xperf and WPR are available, the wrapper prefers xperf because its explicit buffer settings are more reliable for short smoke runs.

For longframe/root-cause runs, use the xperf latency preset so the trace includes context-switch, ready-thread, disk, and file-IO evidence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-deadworks-shadow-server.ps1 `
  -RequireProfiler `
  -Profiler Xperf `
  -XperfPreset Latency
```

To require a profiler and fail if ETW cannot start:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1 -RequireProfiler
```

To disable profiling explicitly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1 -NoProfile
```

## Native stack test suite

Use the native stack suite to exercise the profiling/debugging harnesses together:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-dwrt-native-stack.ps1
```

By default this runs Rust workspace tests, clippy, the runtime C ABI smoke, and the DWRT host resolver/bootstrap smoke. Add live-server gates when you need to validate injection and hook installation against a real dedicated server:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-dwrt-native-stack.ps1 `
  -IncludeLiveServer `
  -IncludeHookInstall `
  -WaitServerModuleSeconds 45 `
  -HoldSeconds 3 `
  -TimeoutSeconds 90
```

The suite writes `dwrt-native-stack-test.json`, `dwrt-native-stack-test.md`, per-step logs, and child smoke artifacts under `research/benchmarks/runs/<timestamp>-dwrt-native-stack-test/`.

Small curated examples of ignored run artifacts live under `research/benchmarks/artifacts/`. Do not commit full run directories or ETW `.etl` files.

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

## Reports

- `runtime-baseline-20260530.md` — Rust runtime route/trace microbenchmark baseline.
- `dwrt-host-signature-smoke-20260530.md` — DWRT-native runtime probe ABI and `server.dll` signature resolver smoke.
- `dwrt-live-server-bootstrap-20260530.md` — DWRT host loaded into a real running dedicated server, resolving live `server.dll` without Deadworks.
- `dwrt-live-hook-install-20260530.md` — first DWRT-owned live-server probe hook install smoke without Deadworks.
- `dwrt-manual-probe-session-20260531.md` — real client manual gameplay probe session with live damage/output counters.
- `deadworks-shadow-boot-20260530.md` — real server boot-only DWRT load proof.
- `client-shadow-smoke-attempt-20260530-invalid.md` — invalid client attempt caused by stale server processes.
- `client-shadow-smoke-20260530.md` — first valid client-connected shadow route proof and longframe classification.

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
