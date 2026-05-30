# Client-Connected DWRT Shadow Smoke Checklist

Purpose: generate non-zero DWRT shadow route counters from the real game client while profiling the whole server run.

## Server command

Run from an elevated/admin shell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-deadworks-shadow-server.ps1 `
  -RequireProfiler `
  -TimeoutSeconds 180 `
  -DeadworksExe 'C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadworks_dwrt.exe' `
  -UsercmdMountMask 2 `
  -NetOutgoingSerializedId 72 `
  -NetIncomingSerializedId 33
```

This sets DWRT shadow mode only. It does not mutate or block gameplay.

## Client actions

Do not automate the client with `SendKeys`.

Manual steps:

1. Start the Deadlock client.
2. Connect to the local dedicated server on port `27067` using the normal local-server flow/console command you use for Deadworks testing.
3. Join a hero/session if prompted.
4. Move/look around for at least 10 seconds to generate usercmd batches.
5. Send one chat message or any harmless action that generates user/net traffic.
6. Wait until the server script times out or stop after at least one `[DWRT] shadow summary` appears.

## Expected server log evidence

Look for:

```txt
[DWRT] loaded runtime abi=1 ...
[DWRT] shadow summary net(no=... fast=... ser=... both=...) usercmd(no=... count=... fast=... full=... both=...)
```

With `-UsercmdMountMask 2`, `usercmd fast` should become non-zero after movement.

With `-NetOutgoingSerializedId 72`, outgoing `svc_UserMessage` route counts should contribute to `ser` if that path fires.

## Artifacts

The script writes to:

```txt
research/benchmarks/runs/<timestamp>-deadworks-shadow-server/
  server.log
  profile/*.json
  profile/*.etl
```

The ETW `.etl` is not committed because it may contain local paths/process data. Summaries should be copied into a committed markdown report after review.
