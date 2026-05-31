# DWRT host signature smoke

Date: 2026-05-30

Purpose: prove the DWRT-owned native path can load `dwrt_runtime.dll`, exercise the new count-only probe ABI, and resolve the current `server.dll` signatures without Deadworks in the measured process.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-host.ps1 -NoProfile -MappedModuleCheck
```

`-NoProfile` was used for this compile/functional smoke only. The script can be profiled via `scripts/profile-dwrt-command.ps1` for later overhead runs.

## Result

Run artifact:

- Bootstrap DLL summary: `research/benchmarks/runs/20260530-210558-host-smoke/dwrt-host-bootstrap.json`
- Resolver smoke summary: `research/benchmarks/runs/20260530-210558-host-smoke/dwrt-host-smoke.json`

Summary:

- `dwrt_host.dll` loaded successfully via the bootstrap smoke executable.
- `dwrt_runtime.dll` loaded successfully through the host DLL and direct resolver smoke.
- DWRT ABI version: `1`.
- Probe ABI smoke passed:
  - unmounted damage event returned no-interest before counter increments;
  - mounted damage and entity-input events returned counted;
  - snapshot counters matched expected values.
- Required file-backed `server.dll` signatures: `0` failures.
- Required mapped-module `server.dll` signatures: `0` failures.
- The mapped-module check used `LoadLibraryExW(..., DONT_RESOLVE_DLL_REFERENCES)` to validate the same resolver mode needed for an in-process host without calling server code in the smoke process.
- `server.dll` hash in smoke summary:
  - FNV-1a64: `0xc5cd75fec7ce0577`
  - timestamp: `0x6a188c2f`
  - image base: `0x180000000`

Validated current-build RVAs include:

- `CBaseEntity::TakeDamageOld` -> `0xc6ba60`
- `CEntityInstance::AcceptInput` -> `0x1f176c0`
- `CEntityIOOutput::FireOutputInternal` -> `0x1f1cee0`
- `CModifierProperty::AddModifier` -> `0x14d5d30`
- `CEntitySystem::CreateEntityByName` -> `0x17c33e0`
- `CEntitySystem::QueueSpawnEntity` -> `0x1f0ddb0`
- `CEntitySystem::ExecuteQueuedCreation` -> `0x1f06120`
- `CTakeDamageInfo::Ctor` -> `0x1addd40`
- `CCitadelGameRules::BuildGameSessionManifest` -> `0x8f7410`
- `CBasePlayerController::ProcessUsercmds` -> `0x174ac50`
- `CCitadelPlayerPawn::AbilityThink` -> `0xa2e060`
- `TraceShape` -> `0x1803c00`
- optional `CBaseEntity::EmitSoundParams` -> `0x18cd210`

## Notes

This is still not an in-process server hook run. It now covers the standalone DWRT host DLL bootstrap, file-backed PE scanning, and mapped-image RVA scanning, proving the DWRT-native resolver/probe base needed before adding a real hook installer. No Deadworks hook chassis is used.
