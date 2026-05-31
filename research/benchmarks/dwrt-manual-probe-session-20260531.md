# DWRT manual gameplay probe session — 2026-05-31

## Purpose

Exercise DWRT-owned live-server probe hooks with a real client and manual gameplay before starting modding/mutation tests.

## Harness

- Script: `scripts/start-dwrt-manual-probe-session.ps1`
- Child smoke: `scripts/smoke-dwrt-live-server.ps1`
- Server args: `-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 +spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 +hostport 27068 +map dl_midtown`
- Probe hooks installed:
  - `CBaseEntity::TakeDamageOld`
  - `CEntityInstance::AcceptInput`
  - `CEntityIOOutput::FireOutputInternal`
- Probe mount mask: `7` (`damage | entity_input | entity_output`)
- Profiling: disabled for this interactive run (`-NoProfile`)
- Snapshot cadence: 5 seconds, off hot path via remote snapshot polling

## Artifact directory

```txt
research/benchmarks/runs/20260531-110255-dwrt-manual-probe-session/
```

Key files:

```txt
dwrt-live-host.json
dwrt-live-injector.json
dwrt-manual-probe-snapshots.jsonl
manual-probe-session.log
manual-probe-session.err.log
```

## Result

The manual session ended cleanly by stop file after the game was finished.

Final injector summary:

```txt
ok=true
finishReason=stop-file
initialized=1
runtimeLoaded=1
runtimeProbeOk=1
signaturesChecked=1
signatureRequiredFailures=0
usedLiveServerModule=1
usedMappedFileFallback=0
hookInstallAttempts=3
hooksInstalled=3
hookInstallFailures=0
callbackEntries=56130
callbackRecursiveEntries=20691
callbackMaxDepth=3
damageSeen=15420
damageCounted=15420
entityInputSeen=0
entityInputCounted=0
entityOutputSeen=20019
entityOutputCounted=20019
```

## Interpretation

- The live DWRT hook install path survived a real client manual gameplay session.
- Damage probe path is validated with non-zero, high-count gameplay traffic.
- Entity output probe path is validated with non-zero, high-count gameplay traffic.
- Entity input probe did not fire in this single-player/manual scenario.
- Recursive callback entries are normal gameplay evidence for these hook boundaries, not a crash by themselves.
- The recursion guard worked: recursive callback entries were tracked and did not cause unbounded recursion; max observed depth was `3`.
- Touch was not measured in this session because the installed hook set does not include touch and the mount mask did not include `DWRT_PROBE_MOUNT_ENTITY_TOUCH`.

## Prior interrupted/closed session note

The earlier `20260531-104543-dwrt-manual-probe-session` child server was closed by the harness, not by a server crash:

- first cause: the configured 900s session limit aligned with late gameplay connection;
- second cause: the strict `callbackRecursiveEntries == 0` validation treated normal gameplay recursion as fatal at shutdown.

The harness was adjusted so manual gameplay sessions can allow recursive callback evidence while still recording it.

## Follow-ups

- Add per-hook recursion counters so nested damage/output paths can be separated.
- Add callback timing counters and max-duration gates.
- Add a deterministic entity-input trigger or a known map action that calls `AcceptInput`.
- Add touch hook descriptors only after RE validation of `StartTouch`/`EndTouch` signatures.
- Repeat one run with ETW profiling enabled once the scenario is stable.
