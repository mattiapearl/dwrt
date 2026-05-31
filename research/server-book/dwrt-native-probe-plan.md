# DWRT-native probe plan

Date: 2026-05-30

Decision: map/NPC/FFA probing should run through DWRT-owned code, not the Deadworks hook chassis. Deadworks remains useful reference evidence, but it must not be in the measured path when answering whether DWRT works and how heavy it is on the server.

## Current readiness

Ready today:

- `dwrt_runtime.dll` C ABI exists.
- Native `dwrt-shim` can load the Rust runtime and route usercmd/net interest checks.
- DWRT runtime now exposes count-only probe C ABI calls for damage, entity input, entity output, and touch summaries.
- `native/dwrt-host` now builds `dwrt_host.dll`, a minimal DWRT-owned bootstrap DLL with explicit initialize/shutdown exports and no heavy `DllMain` work.
- `native/dwrt-host` can scan the current `server.dll` PE file and mapped-image/module memory with a DWRT-owned manifest, failing closed on missing/non-unique/drifted required signatures.
- `dwrt_host.dll` resolves live `GetModuleHandleW(L"server.dll")` when present and supports mapped-file fallback only for smoke tests.
- `scripts/smoke-dwrt-host.ps1 -MappedModuleCheck` verifies runtime load, probe ABI counters, host-DLL bootstrap, file-backed RVAs, and mapped-module RVAs without Deadworks in the smoke process.
- `scripts/smoke-dwrt-live-server.ps1` launches the real `deadlock.exe` dedicated server, injects `dwrt_host.dll`, initializes DWRT, resolves live `server.dll`, validates host testpoints, and records a latency-profiled run artifact.
- `scripts/smoke-dwrt-live-server.ps1 -InstallProbeHooks` installs the first DWRT-owned allowlisted probe hooks in the live server with no mounted interests.
- Profiling scripts exist for native smoke and real-server shadow runs.
- Static RE identified candidate hook/function boundaries for damage, entity I/O, entity lifecycle, NPC spawning, Pulse, and FFA gates.

Not ready yet for pure DWRT map/NPC/FFA probing:

- DWRT-native hook installer exists for the first allowlisted probe hooks, but only has a short no-interest install smoke so far;
- no production launcher packaging; current live-server path is a dev smoke/injector only;
- hook callbacks are wired to count-only probe ABI and recursion guards, but gameplay-triggered callback evidence is still pending;
- no native extraction code that converts hook arguments into the compact probe ABI structs;
- no throttled probe artifact writer for entity/damage/I/O summaries.

Therefore the base layer is ready for runtime load/probe ABI/signature-resolver/live-server bootstrap measurements, but not yet for full map/NPC/FFA probes without adding a thin native hook/probe layer.

## Minimal base layer before pure DWRT probes

1. **DWRT host/bootstrap DLL** *(minimal DLL + live-server dev loader implemented 2026-05-30)*
   - loads `dwrt_runtime.dll`;
   - initializes one runtime;
   - owns hook install/uninstall;
   - writes a startup/probe summary artifact;
   - avoids synchronous hot-path logging;
   - live-server smoke validates `usedLiveServerModule=true` and `usedMappedFileFallback=false`;
   - next: add hook ownership.

2. **Signature resolver** *(file-backed and mapped-module smoke implemented 2026-05-30)*
   - resolve known `server.dll` functions from manifest/patterns;
   - emit module hash/build id and resolved addresses;
   - fail closed when signatures do not match;
   - mapped-image scanning now validates loaded-module RVA behavior;
   - `dwrt_host.dll` can call the module resolver against `GetModuleHandleW(L"server.dll")`;
   - next: verify that path inside a real server process.

3. **Hook installer** *(first live no-interest install smoke passed 2026-05-30)*
   - minimal C++ trampoline layer, still DWRT-owned;
   - do not hand-roll unsafe x64 instruction relocation for production; use a small audited hook backend with license retained, then wrap it behind DWRT's allowlisted descriptors;
   - current hooks:
     - `CBaseEntity::TakeDamageOld`;
     - `CEntityInstance::AcceptInput`;
     - `CEntityIOOutput::FireOutputInternal`;
   - optional `CBaseEntity::StartTouch` / `EndTouch` vtable hooks after first pass;
   - no broad detours beyond explicitly mounted probes;
   - next: run longer/no-interest callback exercise and then mount damage/I/O probes.

4. **Rust probe ABI** *(count-only exports implemented 2026-05-30)*
   - C exports for count-only hot callbacks:
     - damage event summary;
     - entity input/output summary;
     - touch summary;
     - entity spawn/lifecycle summary if native listener is added;
   - current exports return before counter increments when unmounted, then count mounted seen/counted events and expose snapshots;
   - next: add native extraction filters by class/designer/input/output/team;
   - ring-buffer or counters first, serialized records only when mounted.

5. **Probe artifact writer**
   - periodic summary JSON/JSONL outside hot callbacks;
   - include QPC timing, route counts, dropped-record counts, and max callback time;
   - no stdout/file writes from hook callbacks.

6. **Live-like launcher/profiler**
   - launch real server with DWRT host only;
   - xperf/WPR optional but preferred;
   - no broken redirected-console harness.

## First probes

Order:

1. boot-only DWRT host load, no hooks;
2. install hooks, no interests, verify near-zero callback work;
3. mount damage count-only probe;
4. mount filtered entity I/O probe for trigger/shop/teleport/bounce/trooper names;
5. test `mp_friendlyfire 0/1` same-team damage visibility;
6. test optional friendly-fire modifier/state only if exposed safely;
7. compare overhead matrix:
   - no DWRT;
   - DWRT host loaded, no hooks;
   - hooks installed, no interests;
   - damage probe mounted;
   - entity I/O probe mounted.

## Guardrails

- Do not use Deadworks in the measured path.
- Do not expose raw memory, vtable indices, or signature hooks to public scripts.
- Hook callbacks must be interest-gated and allocation-free where practical.
- No synchronous logging in hot callbacks.
- Every smoke/benchmark should produce profiler or timing artifacts.
