# Client Shadow Smoke Attempt — 2026-05-30 — Invalid

Run artifact path:

```txt
research/benchmarks/runs/20260530-130408-deadworks-shadow-server/
```

## Result

This run is **not valid** for attributing client longframes to DWRT.

The server log shows DWRT loaded, but the requested game port was already occupied:

```txt
[DWRT] loaded runtime abi=1 path='C:\Code\dwrt\target\release\dwrt_runtime.dll' timing=1 qpcHz=10000000 slowThresholdTicks=1000 usercmdMountMask=0x2
Cannot create listen socket.  Failed to bind socket.  Error code 0x00002740.
Network socket 'server' opened on port 27015
```

There were no `[DWRT] shadow summary` lines and no clear client-connected route counters in this run.

## Root cause

The profiler wrapper stopped only the root `cmd.exe` on timeout. Child `deadworks_dwrt.exe` processes survived, leaving multiple stale servers bound to the target port:

```txt
deadworks_dwrt.exe from earlier smoke runs still listening on 27067
```

The client likely connected to a stale server, not the profiled run.

## Fix

Committed in `4909c15`:

- `scripts/profile-dwrt-command.ps1` now kills the full process tree on timeout.
- `scripts/smoke-deadworks-shadow-server.ps1` now preflights existing matching Deadworks processes and fails loudly unless `-KillExisting` is passed.

## Next attempt

Re-run the client-connected smoke after confirming no stale `deadworks_dwrt.exe` processes are alive.
