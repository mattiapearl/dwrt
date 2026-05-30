# Client Shadow Smoke — 2026-05-30

Run artifact path:

```txt
research/benchmarks/runs/20260530-131431-deadworks-shadow-server/
```

Profiler artifact:

```txt
research/benchmarks/runs/20260530-131431-deadworks-shadow-server/profile/20260530-131434-deadworks-shadow-server.etl
```

Raw ETW traces are intentionally not committed because ETW files can contain local paths/process data.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-deadworks-shadow-server.ps1 `
  -RequireProfiler `
  -KillExisting `
  -TimeoutSeconds 180 `
  -DeadworksExe 'C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadworks_dwrt.exe' `
  -UsercmdMountMask 2 `
  -NetOutgoingSerializedId 72 `
  -NetIncomingSerializedId 33
```

The wrapper used xperf, captured the full 180 second run, and stopped the process tree on timeout.

## Result

This is the first valid client-connected DWRT shadow run.

Evidence:

```txt
[deadworks] [INF] [DWRT] loaded runtime abi=1 path='C:\Code\dwrt\target\release\dwrt_runtime.dll' timing=1 qpcHz=10000000 slowThresholdTicks=1000 usercmdMountMask=0x2
[deadworks] [INF] StartupServer (map: dl_midtown)
```

There was no bind failure in this run. The client generated non-zero DWRT shadow route counters:

```txt
[2026-05-30 13:17:30.855] [deadworks] [INF] [DWRT] shadow summary net(no=71148 fast=0 ser=0 both=0 maxTicks=1191 slow=2) usercmd(no=0 count=0 fast=10878 full=0 both=0 maxTicks=1052 slow=1)
```

What this proves:

- real Deadlock dedicated server booted with the temporary Deadworks-hosted DWRT shadow shim;
- `dwrt_runtime.dll` loaded in-process and passed the ABI check;
- net shadow route was called in-game (`net.no_interest = 71148`);
- usercmd shadow route was called in-game (`usercmd.fast_read = 10878`);
- the run was captured under xperf with zero lost ETW events.

The configured serialized net interests did not match traffic in this scenario (`fast=0`, `ser=0`, `both=0`). This run proves the net hook/no-interest path, not a serialized net-message hit.

## DWRT timing counters

The shadow timing threshold was 100,000 ns. With `qpcHz=10000000`, that is `1000` QPC ticks.

Observed maximum DWRT route timings:

```txt
net maxTicks=1191      # about 119.1 us
usercmd maxTicks=1052  # about 105.2 us
```

Slow-call counters were tiny relative to route volume:

```txt
net slow=2 / 71148 calls
usercmd slow=1 / 10878 calls
```

These counters do not explain the reported 100 ms+ server longframes.

## Main-thread stall analysis

Further xperf sample-dump analysis found that the gameplay longframes were **off-CPU/main-thread stalls**, not server CPU work and not DWRT route work.

Server main thread in this run: `deadworks_dwrt.exe` tid `14504`.

Relevant sample gaps:

```txt
156.864299s -> 156.993213s = 128.914ms off-CPU gap
174.579750s -> 175.161112s = 581.362ms off-CPU gap
```

Those line up with the gameplay longframes:

```txt
156.809662 IN_PROGRESS_LONG_FRAME TOOK 143.940ms DURING GAMEPLAY.
174.976440 IN_PROGRESS_LONG_FRAME TOOK 597.398ms DURING GAMEPLAY.
```

The severe 597ms frame has the strongest smoking gun: the last server-main-thread CPU sample before the 581ms gap is in the Windows file-system path, reached from engine/tier0 code:

```txt
Ntfs.sys
FLTMGR.SYS
ntoskrnl.exe
ntdll.dll
KernelBase.dll
engine2.dll
engine2.dll
tier0.dll
engine2.dll
...
deadworks_dwrt.exe main loop
```

So the severe frame is best explained as a synchronous file/console/log I/O stall on the server main thread. In this smoke harness stdout/stderr were redirected to `server.log`, while Source's console input path emitted per-frame warnings:

```txt
CTextConsoleWin::GetLine: !GetNumberOfConsoleInputEvents
```

This run wrote `11327` of those warning lines. A no-client 90s control run with the same harness still produced `5585` of the warning lines, confirming the spam is harness-induced and not caused by DWRT route subscriptions or client traffic.

The netchan high-water messages are therefore treated as a consequence of the stalled server frame: while the server frame is delayed, outgoing traffic backs up and high-water logging fires when the server resumes/flushes.

## Longframes observed

The server log recorded two gameplay longframes matching the manual report window:

```txt
156.809662 IN_PROGRESS_LONG_FRAME TOOK 143.940ms DURING GAMEPLAY.
174.976440 IN_PROGRESS_LONG_FRAME TOOK 597.398ms DURING GAMEPLAY.
```

Nearby DWRT summaries did not show a corresponding jump in max route time:

- before/after the 143.940 ms frame, net max stayed at `1191` ticks and net slow stayed at `1`;
- before/after the 597.398 ms frame, net max stayed at `1191` ticks and net slow stayed at `2`;
- usercmd max/slow stayed at `1052` ticks / `1`.

The longframes correlate with Source/Deadlock netchan queue high-water logs, especially the second frame:

```txt
Netchan queued message new high water mark reached by iIrsMcxc at 33 messages
Netchan queued message new high water mark reached by iIrsMcxc at 41 messages
Netchan queued message new high water mark reached by iIrsMcxc at 50 messages
Netchan queued message new high water mark reached by iIrsMcxc at 62 messages
Netchan queued message new high water mark reached by iIrsMcxc at 77 messages
Netchan queued message new high water mark reached by iIrsMcxc at 97 messages
Netchan queued message new high water mark reached by iIrsMcxc at 121 messages
Netchan queued message new high water mark reached by iIrsMcxc at 150 messages
```

The smoke harness also produced heavy console-input warning spam:

```txt
CTextConsoleWin::GetLine: !GetNumberOfConsoleInputEvents
```

Count in this run:

```txt
11327 lines
```

## ETW notes

Trace stats:

```txt
Start time (Local)   : 2026/05/30:13:14:34.5297441
End time (Local)     : 2026/05/30:13:17:36.1359332
# Lost Buffers       : 0
# Lost Events        : 0
Number of Processors : 28
```

xperf CPU sample windows around the longframes did not implicate DWRT:

- `dwrt_runtime.dll` had no meaningful samples around either gameplay longframe;
- `coreclr.dll` was not the source of the long stall;
- server-main-thread samples stop for ~581ms across the severe frame;
- the last sample before that gap is in `Ntfs.sys`/`FLTMGR.SYS` from engine/tier0 code;
- server samples outside the gaps were in normal engine modules such as `server.dll`, `vphysics2.dll`, `animationsystem.dll`, `engine2.dll`, `networksystem.dll`, and `tier0.dll`.

Limitation: this xperf profile was started with `PROC_THREAD+LOADER+PROFILE` only. It did not include file-name, context-switch, or ready-thread data, so it identifies the class of stall (`NTFS`/file-system I/O on the main thread) but not the exact file handle or minifilter delay.

Local analysis outputs were written under:

```txt
target/analysis/20260530-131431/
```

## Classification

Current classification: **not DWRT route overhead**.

Cause for the severe gameplay frame: **server main-thread synchronous file/console/log I/O stall in the smoke harness**.

Secondary symptom: **netchannel queue backpressure while the stalled frame is not advancing**.

Confidence: high for ruling out DWRT and high that the severe frame entered the Windows file-system path immediately before the stall. Confidence is medium on the exact file/minifilter because this trace did not include file-name or context-switch providers.

## Follow-up

Do not use this redirected-console harness for final gameplay overhead numbers. Repeat with:

- console-input spam removed or avoided;
- file/console output not synchronously written by the server main thread where possible;
- xperf context-switch, ready-thread, and file-IO providers enabled in addition to CPU sampling;
- the same DWRT shadow counters;
- a no-DWRT or DWRT-disabled comparison run under the same launch harness.
