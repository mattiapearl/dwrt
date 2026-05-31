# DWRT live-server hook install smoke

Date: 2026-05-30

Purpose: prove the DWRT-owned hook backend can install the first allowlisted probe hooks in a real running Deadlock dedicated-server process, with runtime/signature gates and recursion testpoints active, without Deadworks in the measured path.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-dwrt-live-server.ps1 `
  -InstallProbeHooks `
  -WaitServerModuleSeconds 45 `
  -HoldSeconds 3 `
  -TimeoutSeconds 90
```

The script launches the real `deadlock.exe`, waits for live `server.dll`, injects `dwrt_host.dll`, initializes DWRT, resolves live signatures, installs the allowlisted probe hooks, snapshots validation counters, then shuts the child process down. It does not redirect server stdout/stderr.

## Result

Run artifact:

- `research/benchmarks/runs/20260530-210304-dwrt-live-server/dwrt-live-host.json`
- `research/benchmarks/runs/20260530-210304-dwrt-live-server/dwrt-live-injector.json`
- `research/benchmarks/runs/20260530-210304-dwrt-live-server/profile/20260530-210319-dwrt-live-server.json`
- ETW: `research/benchmarks/runs/20260530-210304-dwrt-live-server/profile/20260530-210319-dwrt-live-server.etl` *(not committed)*

Summary gates passed:

- live `server.dll` resolved: `usedLiveServerModule = true`;
- mapped-file fallback not used: `usedMappedFileFallback = false`;
- required signatures: `0` failures;
- runtime probe ABI smoke passed;
- hook install attempts: `3`;
- hooks installed: `3`;
- hook install failures: `0`;
- initialize calls: `1`;
- reentrant initialize rejects: `0`;
- callback recursive entries: `0`;
- profiler metadata exit code: `0`.

Hooks installed:

- `CBaseEntity::TakeDamageOld`
- `CEntityInstance::AcceptInput`
- `CEntityIOOutput::FireOutputInternal`

## Notes

This is still a short no-interest hook-install smoke, not a gameplay probe. The run did not exercise hook callbacks (`callbackEntries = 0`) during the short hold window. Recursion detection itself is covered by the host testpoints smoke in `scripts/smoke-dwrt-host.ps1`.

The hook backend uses a local SafetyHook + Zydis vendor copy behind DWRT's allowlisted descriptors. Before release, replace/validate the vendor source with explicit upstream license/version metadata.
