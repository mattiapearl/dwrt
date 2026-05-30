# DWRT Runtime Baseline — 2026-05-30

Environment: local Windows/MSVC machine, elevated shell, release build.
Profiler: xperf ETW via `scripts/profile-dwrt-command.ps1 -RequireProfiler`.

Raw ETW traces are intentionally not committed because ETW files can contain local paths/process data.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/bench-dwrt-runtime.ps1 -Iterations 1000000 -RequireProfiler
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-shim.ps1 -RequireProfiler
```

## `dwrt-bench` results

Run artifact path:

```txt
research/benchmarks/runs/20260530-122748/
```

| case | ns/op | ops/s | checksum |
| --- | ---: | ---: | ---: |
| net.no_interest | 0.563 | 1,776,514,479 | 0 |
| net.fast_user_message | 1.909 | 523,944,252 | 1,000,000 |
| net.serialized_envelope | 1.386 | 721,656,924 | 2,000,000 |
| net.fast_and_serialized_user_message | 1.706 | 586,200,832 | 3,000,000 |
| usercmd.count_only | 0.553 | 1,807,664,497 | 1,000,000 |
| usercmd.fast_read | 0.550 | 1,819,505,095 | 2,000,000 |
| usercmd.fast_and_full | 0.551 | 1,814,552,713 | 4,000,000 |
| trace.ring_push | 4.475 | 223,453,700 | 29,397,117,440 |

## Native shim smoke

Run artifact path:

```txt
research/benchmarks/runs/20260530-123020-shim-smoke/
```

Smoke output confirmed:

```txt
[DWRT] loaded runtime abi=1
[DWRT] runtime created
[DWRT] shadow net route no_interest=0 fast=1 fast_serialized=3
[DWRT] shadow usercmd route count=1 fast=2 fast_full=4
[dwrt-shim-smoke] counters net_no_interest=1 net_fast=1 net_serialized=0 net_fast_serialized=1 usercmd_no_work=0 usercmd_count=1 usercmd_fast=1 usercmd_full=0 usercmd_fast_full=1 qpc_hz=10000000 net_max_ticks=1 usercmd_max_ticks=1 net_slow=0 usercmd_slow=0
[dwrt-shim-smoke] OK
```

## Notes

- These are microbenchmarks and native smoke checks, not in-game overhead numbers.
- The current route costs are small enough that the next important measurement is in-process hook overhead and any longframe correlation under the real dedicated server.
- xperf was preferred over WPR after one short WPR smoke trace reported dropped events.
