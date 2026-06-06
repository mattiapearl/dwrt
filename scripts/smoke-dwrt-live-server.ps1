param(
    [string]$Configuration = "release",

    [string]$ServerExe = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadlock.exe",

    [string]$ServerDll = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\bin\win64\server.dll",

    [string]$RuntimeDll = "",

    [string]$ServerArgs = "-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 +spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 +hostport 27068 +map dl_midtown",

    [int]$WaitServerModuleSeconds = 60,

    [int]$HoldSeconds = 5,

    [int]$PollSeconds = 0,

    [uint32]$ProbeMountMask = 0,

    [string]$SnapshotJsonl = "",

    [string]$StopFile = "",

    [int]$TimeoutSeconds = 120,

    [switch]$NoProfile,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [ValidateSet("Cpu", "Latency")]
    [string]$XperfPreset = "Latency",

    [string]$OutputDir = "",

    [switch]$InstallProbeHooks,

    [switch]$AllowRecursiveCallbacks,

    [switch]$WalkerPatrolExperiment,

    [ValidateSet("Velocity", "OriginNudge")]
    [string]$WalkerPatrolMode = "Velocity",

    [int]$WalkerPatrolStride = 16,

    [string]$WalkerPatrolVelocities = "900,0,0;0,900,0;-900,0,0;0,-900,0",

    [switch]$FriendlyFireExperiment,

    [ValidateSet(0, 2, 3)]
    [int]$FriendlyFireLocalTeam = 2,

    [switch]$TargetSourceTeamSpoofExperiment,

    [switch]$TargetTeamSpoofExperiment,

    [ValidateSet("Opposing", "Neutral")]
    [string]$TargetTeamSpoofMode = "Opposing",

    [switch]$TargetForceSameTeamAllowExperiment,

    [switch]$TargetForceObjectiveAllowExperiment,

    [switch]$TargetNeutralSimulationExperiment,

    [switch]$TargetBitsetAllowExperiment,

    [switch]$KillExisting
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$targetDir = Join-Path $repo "target\$Configuration"
if ([string]::IsNullOrWhiteSpace($RuntimeDll)) {
    $RuntimeDll = Join-Path $targetDir "dwrt_runtime.dll"
}
$runtimeHeaderDir = Join-Path $repo "crates\dwrt-runtime\include"
$hostDir = Join-Path $repo "native\dwrt-host"
$shimDir = Join-Path $repo "native\dwrt-shim"
$vendorDir = Join-Path $repo "native\vendor"
$outDir = Join-Path $repo "target\live-server"
$objDir = Join-Path $outDir "obj"
$hostDllObjDir = Join-Path $objDir "hostdll"
$injectorObjDir = Join-Path $objDir "injector"
$hostDll = Join-Path $outDir "dwrt_host.dll"
$hostImportLib = Join-Path $outDir "dwrt_host.lib"
$injectorExe = Join-Path $outDir "dwrt_live_server_smoke.exe"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-dwrt-live-server"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repo $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$profileDir = Join-Path $OutputDir "profile"
$hostSummaryPath = Join-Path $OutputDir "dwrt-live-host.json"
$injectorSummaryPath = Join-Path $OutputDir "dwrt-live-injector.json"

New-Item -ItemType Directory -Force $outDir | Out-Null
New-Item -ItemType Directory -Force $objDir | Out-Null
New-Item -ItemType Directory -Force $hostDllObjDir | Out-Null
New-Item -ItemType Directory -Force $injectorObjDir | Out-Null
New-Item -ItemType Directory -Force $OutputDir | Out-Null

if (!(Test-Path $ServerExe)) { throw "Missing server executable: $ServerExe" }
if (!(Test-Path $ServerDll)) { throw "Missing server.dll: $ServerDll" }

$existing = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $ServerExe })
if ($existing.Count -gt 0) {
    if ($KillExisting) {
        Write-Warning "Stopping existing process(es) for '$ServerExe': $($existing.Id -join ', ')"
        $existing | Stop-Process -Force
        Start-Sleep -Seconds 1
    }
    else {
        Write-Warning "Existing process(es) for '$ServerExe' are already running ($($existing.Id -join ', ')); the smoke will launch and inject only its own child process."
    }
}

Write-Host "[dwrt-live-server] cargo build -p dwrt-runtime --$Configuration"
cargo build -p dwrt-runtime --$Configuration
if ($LASTEXITCODE -ne 0) { throw "cargo build failed with exit code $LASTEXITCODE" }
if (!(Test-Path $RuntimeDll)) { throw "Missing runtime DLL: $RuntimeDll" }

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if (!(Test-Path $vsDevCmd)) {
    $vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
}
if (!(Test-Path $vsDevCmd)) { throw "Could not find VsDevCmd.bat" }

$hostDllSources = @(
    (Join-Path $shimDir "dwrt_shadow_shim.cpp"),
    (Join-Path $vendorDir "Zydis.c"),
    (Join-Path $vendorDir "safetyhook.cpp"),
    (Join-Path $hostDir "dwrt_signature_scanner.cpp"),
    (Join-Path $hostDir "dwrt_probe_manifest.cpp"),
    (Join-Path $hostDir "dwrt_hook_backend.cpp"),
    (Join-Path $hostDir "dwrt_host_testpoints.cpp"),
    (Join-Path $hostDir "dwrt_walker_patrol.cpp"),
    (Join-Path $hostDir "dwrt_friendly_fire.cpp"),
    (Join-Path $hostDir "dwrt_target_probe.cpp"),
    (Join-Path $hostDir "dwrt_host.cpp")
)
$hostDllSourceArgs = ($hostDllSources | ForEach-Object { '"{0}"' -f $_ }) -join " "
$compileHostDll = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /std:c++latest /EHsc /W4 /WX /wd4201 /wd4834 /DDWRT_HOST_BUILD /LD /Fo"{1}\\" /I"{2}" /I"{3}" /I"{4}" /I"{5}" {6} /link /OUT:"{7}" /IMPLIB:"{8}"' -f $vsDevCmd, $hostDllObjDir, $runtimeHeaderDir, $shimDir, $hostDir, $vendorDir, $hostDllSourceArgs, $hostDll, $hostImportLib

Write-Host "[dwrt-live-server] compile DWRT host DLL"
cmd /c $compileHostDll
if ($LASTEXITCODE -ne 0) { throw "DWRT host DLL compile failed with exit code $LASTEXITCODE" }

$injectorSource = Join-Path $hostDir "inject_smoke.cpp"
$compileInjector = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"{1}\\" /I"{2}" /I"{3}" "{4}" /link /OUT:"{5}"' -f $vsDevCmd, $injectorObjDir, $hostDir, $runtimeHeaderDir, $injectorSource, $injectorExe

Write-Host "[dwrt-live-server] compile live-server injector smoke"
cmd /c $compileInjector
if ($LASTEXITCODE -ne 0) { throw "live-server injector smoke compile failed with exit code $LASTEXITCODE" }

$runArgs = @(
    "--server-exe", $ServerExe,
    "--server-dll", $ServerDll,
    "--host", $hostDll,
    "--runtime", $RuntimeDll,
    "--host-summary", $hostSummaryPath,
    "--injector-summary", $injectorSummaryPath,
    "--server-args", $ServerArgs,
    "--wait-server-module-seconds", $WaitServerModuleSeconds.ToString(),
    "--hold-seconds", $HoldSeconds.ToString(),
    "--poll-seconds", $PollSeconds.ToString(),
    "--probe-mount-mask", $ProbeMountMask.ToString()
)
if (![string]::IsNullOrWhiteSpace($SnapshotJsonl)) { $runArgs += @("--snapshot-jsonl", $SnapshotJsonl) }
if (![string]::IsNullOrWhiteSpace($StopFile)) { $runArgs += @("--stop-file", $StopFile) }
if ($InstallProbeHooks) { $runArgs += "--install-probe-hooks" }
if ($AllowRecursiveCallbacks) { $runArgs += "--allow-recursive-callbacks" }

$oldWalkerExperiment = $env:DWRT_WALKER_PATROL_EXPERIMENT
$oldWalkerStride = $env:DWRT_WALKER_PATROL_STRIDE
$oldWalkerMode = $env:DWRT_WALKER_PATROL_MODE
$oldWalkerOriginNudge = $env:DWRT_WALKER_PATROL_ORIGIN_NUDGE
$oldWalkerVelocities = $env:DWRT_WALKER_PATROL_VELOCITIES
$oldFriendlyFireExperiment = $env:DWRT_FRIENDLY_FIRE_EXPERIMENT
$oldFriendlyFireObjectiveSpoof = $env:DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF
$oldFriendlyFireLocalTeam = $env:DWRT_FRIENDLY_FIRE_LOCAL_TEAM
$oldTargetSourceTeamSpoof = $env:DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF
$oldTargetTeamSpoof = $env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF
$oldTargetTeamSpoofTeam = $env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF_TEAM
$oldTargetBitsetAllow = $env:DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET
$oldTargetForceFilterSameTeamAllow = $env:DWRT_TARGET_PROBE_FORCE_FILTER_SAME_TEAM_ALLOW
$oldTargetForceCallerSameTeamAllow = $env:DWRT_TARGET_PROBE_FORCE_CALLER_SAME_TEAM_ALLOW
$oldTargetForceFilterObjectiveAllow = $env:DWRT_TARGET_PROBE_FORCE_FILTER_OBJECTIVE_ALLOW
$oldTargetForceCallerObjectiveAllow = $env:DWRT_TARGET_PROBE_FORCE_CALLER_OBJECTIVE_ALLOW
$oldTargetForceSecondaryAllow = $env:DWRT_TARGET_PROBE_FORCE_SECONDARY_ALLOW
$oldTargetNeutralSimulation = $env:DWRT_TARGET_PROBE_NEUTRAL_SIMULATION
try {
    if ($WalkerPatrolExperiment) {
        $env:DWRT_WALKER_PATROL_EXPERIMENT = "1"
        $env:DWRT_WALKER_PATROL_STRIDE = [Math]::Max(1, $WalkerPatrolStride).ToString()
        $env:DWRT_WALKER_PATROL_MODE = if ($WalkerPatrolMode -eq "OriginNudge") { "origin-nudge" } else { "velocity" }
        if ($WalkerPatrolMode -eq "OriginNudge") { $env:DWRT_WALKER_PATROL_ORIGIN_NUDGE = "1" } else { Remove-Item Env:DWRT_WALKER_PATROL_ORIGIN_NUDGE -ErrorAction SilentlyContinue }
        $env:DWRT_WALKER_PATROL_VELOCITIES = $WalkerPatrolVelocities
    }
    else {
        Remove-Item Env:DWRT_WALKER_PATROL_EXPERIMENT -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_WALKER_PATROL_STRIDE -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_WALKER_PATROL_MODE -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_WALKER_PATROL_ORIGIN_NUDGE -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_WALKER_PATROL_VELOCITIES -ErrorAction SilentlyContinue
    }

    if ($FriendlyFireExperiment) {
        $env:DWRT_FRIENDLY_FIRE_EXPERIMENT = "1"
        $env:DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF = "1"
        $env:DWRT_FRIENDLY_FIRE_LOCAL_TEAM = $FriendlyFireLocalTeam.ToString()
    }
    else {
        Remove-Item Env:DWRT_FRIENDLY_FIRE_EXPERIMENT -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_FRIENDLY_FIRE_LOCAL_TEAM -ErrorAction SilentlyContinue
    }

    if ($TargetSourceTeamSpoofExperiment) {
        $env:DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF = "1"
    }
    else {
        Remove-Item Env:DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF -ErrorAction SilentlyContinue
    }

    if ($TargetTeamSpoofExperiment) {
        $env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF = "1"
        $env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF_TEAM = if ($TargetTeamSpoofMode -eq "Neutral") { "0" } else { "opposing" }
    }
    else {
        Remove-Item Env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF_TEAM -ErrorAction SilentlyContinue
    }

    if ($TargetBitsetAllowExperiment) {
        $env:DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET = "1"
    }
    else {
        Remove-Item Env:DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET -ErrorAction SilentlyContinue
    }

    if ($TargetForceSameTeamAllowExperiment) {
        $env:DWRT_TARGET_PROBE_FORCE_FILTER_SAME_TEAM_ALLOW = "1"
        $env:DWRT_TARGET_PROBE_FORCE_CALLER_SAME_TEAM_ALLOW = "1"
    }
    else {
        Remove-Item Env:DWRT_TARGET_PROBE_FORCE_FILTER_SAME_TEAM_ALLOW -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_TARGET_PROBE_FORCE_CALLER_SAME_TEAM_ALLOW -ErrorAction SilentlyContinue
    }

    if ($TargetForceObjectiveAllowExperiment) {
        $env:DWRT_TARGET_PROBE_FORCE_FILTER_OBJECTIVE_ALLOW = "1"
        $env:DWRT_TARGET_PROBE_FORCE_CALLER_OBJECTIVE_ALLOW = "1"
    }
    else {
        Remove-Item Env:DWRT_TARGET_PROBE_FORCE_FILTER_OBJECTIVE_ALLOW -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_TARGET_PROBE_FORCE_CALLER_OBJECTIVE_ALLOW -ErrorAction SilentlyContinue
    }

    if ($TargetNeutralSimulationExperiment) {
        $env:DWRT_TARGET_PROBE_NEUTRAL_SIMULATION = "1"
        $env:DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET = "1"
        $env:DWRT_TARGET_PROBE_FORCE_SECONDARY_ALLOW = "1"
    }
    else {
        Remove-Item Env:DWRT_TARGET_PROBE_NEUTRAL_SIMULATION -ErrorAction SilentlyContinue
        Remove-Item Env:DWRT_TARGET_PROBE_FORCE_SECONDARY_ALLOW -ErrorAction SilentlyContinue
    }

    if ($NoProfile) {
        Write-Warning "Running live-server smoke without profiler because -NoProfile was specified."
        & $injectorExe @runArgs
        if ($LASTEXITCODE -ne 0) { throw "live-server smoke failed with exit code $LASTEXITCODE" }
    }
    else {
        $argumentLine = ($runArgs | ForEach-Object {
            if ($_ -match '[\s"]') { '"{0}"' -f ($_ -replace '"', '\"') } else { $_ }
        }) -join " "
        $profileParams = @{
            FilePath = $injectorExe
            ArgumentLine = $argumentLine
            Name = "dwrt-live-server"
            OutputDir = $profileDir
            Profiler = $Profiler
            XperfPreset = $XperfPreset
            TimeoutSeconds = $TimeoutSeconds
        }
        if ($RequireProfiler) { $profileParams.RequireProfiler = $true }
        & (Join-Path $repo "scripts\profile-dwrt-command.ps1") @profileParams
        if ($LASTEXITCODE -ne 0) { throw "profiled live-server smoke failed with exit code $LASTEXITCODE" }
    }
}
finally {
    if ($null -eq $oldWalkerExperiment) { Remove-Item Env:DWRT_WALKER_PATROL_EXPERIMENT -ErrorAction SilentlyContinue } else { $env:DWRT_WALKER_PATROL_EXPERIMENT = $oldWalkerExperiment }
    if ($null -eq $oldWalkerStride) { Remove-Item Env:DWRT_WALKER_PATROL_STRIDE -ErrorAction SilentlyContinue } else { $env:DWRT_WALKER_PATROL_STRIDE = $oldWalkerStride }
    if ($null -eq $oldWalkerMode) { Remove-Item Env:DWRT_WALKER_PATROL_MODE -ErrorAction SilentlyContinue } else { $env:DWRT_WALKER_PATROL_MODE = $oldWalkerMode }
    if ($null -eq $oldWalkerOriginNudge) { Remove-Item Env:DWRT_WALKER_PATROL_ORIGIN_NUDGE -ErrorAction SilentlyContinue } else { $env:DWRT_WALKER_PATROL_ORIGIN_NUDGE = $oldWalkerOriginNudge }
    if ($null -eq $oldWalkerVelocities) { Remove-Item Env:DWRT_WALKER_PATROL_VELOCITIES -ErrorAction SilentlyContinue } else { $env:DWRT_WALKER_PATROL_VELOCITIES = $oldWalkerVelocities }
    if ($null -eq $oldFriendlyFireExperiment) { Remove-Item Env:DWRT_FRIENDLY_FIRE_EXPERIMENT -ErrorAction SilentlyContinue } else { $env:DWRT_FRIENDLY_FIRE_EXPERIMENT = $oldFriendlyFireExperiment }
    if ($null -eq $oldFriendlyFireObjectiveSpoof) { Remove-Item Env:DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF -ErrorAction SilentlyContinue } else { $env:DWRT_FRIENDLY_FIRE_OBJECTIVE_TEAM_SPOOF = $oldFriendlyFireObjectiveSpoof }
    if ($null -eq $oldFriendlyFireLocalTeam) { Remove-Item Env:DWRT_FRIENDLY_FIRE_LOCAL_TEAM -ErrorAction SilentlyContinue } else { $env:DWRT_FRIENDLY_FIRE_LOCAL_TEAM = $oldFriendlyFireLocalTeam }
    if ($null -eq $oldTargetSourceTeamSpoof) { Remove-Item Env:DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_SOURCE_TEAM_SPOOF = $oldTargetSourceTeamSpoof }
    if ($null -eq $oldTargetTeamSpoof) { Remove-Item Env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF = $oldTargetTeamSpoof }
    if ($null -eq $oldTargetTeamSpoofTeam) { Remove-Item Env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF_TEAM -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_TARGET_TEAM_SPOOF_TEAM = $oldTargetTeamSpoofTeam }
    if ($null -eq $oldTargetBitsetAllow) { Remove-Item Env:DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_ALLOW_TARGET_BITSET = $oldTargetBitsetAllow }
    if ($null -eq $oldTargetForceFilterSameTeamAllow) { Remove-Item Env:DWRT_TARGET_PROBE_FORCE_FILTER_SAME_TEAM_ALLOW -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_FORCE_FILTER_SAME_TEAM_ALLOW = $oldTargetForceFilterSameTeamAllow }
    if ($null -eq $oldTargetForceCallerSameTeamAllow) { Remove-Item Env:DWRT_TARGET_PROBE_FORCE_CALLER_SAME_TEAM_ALLOW -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_FORCE_CALLER_SAME_TEAM_ALLOW = $oldTargetForceCallerSameTeamAllow }
    if ($null -eq $oldTargetForceFilterObjectiveAllow) { Remove-Item Env:DWRT_TARGET_PROBE_FORCE_FILTER_OBJECTIVE_ALLOW -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_FORCE_FILTER_OBJECTIVE_ALLOW = $oldTargetForceFilterObjectiveAllow }
    if ($null -eq $oldTargetForceCallerObjectiveAllow) { Remove-Item Env:DWRT_TARGET_PROBE_FORCE_CALLER_OBJECTIVE_ALLOW -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_FORCE_CALLER_OBJECTIVE_ALLOW = $oldTargetForceCallerObjectiveAllow }
    if ($null -eq $oldTargetForceSecondaryAllow) { Remove-Item Env:DWRT_TARGET_PROBE_FORCE_SECONDARY_ALLOW -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_FORCE_SECONDARY_ALLOW = $oldTargetForceSecondaryAllow }
    if ($null -eq $oldTargetNeutralSimulation) { Remove-Item Env:DWRT_TARGET_PROBE_NEUTRAL_SIMULATION -ErrorAction SilentlyContinue } else { $env:DWRT_TARGET_PROBE_NEUTRAL_SIMULATION = $oldTargetNeutralSimulation }
}

if (!(Test-Path $hostSummaryPath)) { throw "Missing host summary: $hostSummaryPath" }
if (!(Test-Path $injectorSummaryPath)) { throw "Missing injector summary: $injectorSummaryPath" }

$hostSummary = Get-Content -Raw $hostSummaryPath | ConvertFrom-Json
$injectorSummary = Get-Content -Raw $injectorSummaryPath | ConvertFrom-Json
if (-not $hostSummary.initialized) { throw "Host summary did not report initialized=true" }
if (-not $hostSummary.runtimeLoaded) { throw "Host summary did not report runtimeLoaded=true" }
if (-not $hostSummary.runtimeProbeOk) { throw "Host summary did not report runtimeProbeOk=true" }
if (-not $hostSummary.signaturesChecked) { throw "Host summary did not report signaturesChecked=true" }
if ([int]$hostSummary.signatureRequiredFailures -ne 0) { throw "Host signatureRequiredFailures=$($hostSummary.signatureRequiredFailures)" }
if (-not $hostSummary.usedLiveServerModule) { throw "Host did not resolve live server.dll module" }
if ($hostSummary.usedMappedFileFallback) { throw "Host unexpectedly used mapped-file fallback in live server smoke" }
if ($InstallProbeHooks) {
    if ([int]$hostSummary.hookInstallAttempts -ne 7) { throw "Expected 7 hook install attempts" }
    if ([int]$hostSummary.hooksInstalled -ne 7) { throw "Expected 7 installed hooks" }
    if ([int]$hostSummary.hookInstallFailures -ne 0) { throw "Hook install failures=$($hostSummary.hookInstallFailures)" }
}
else {
    if ([int]$hostSummary.hookInstallAttempts -ne 0) { throw "Unexpected hook install attempts" }
    if ([int]$hostSummary.hooksInstalled -ne 0) { throw "Unexpected installed hooks" }
}
if ([int64]$hostSummary.testpoints.initializeCalls -ne 1) { throw "Expected exactly one initialize call" }
if ([int64]$hostSummary.testpoints.initializeReentrantRejects -ne 0) { throw "Unexpected reentrant initialize rejection" }
if ([int64]$hostSummary.testpoints.callbackRecursiveEntries -ne 0) { throw "Unexpected recursive callback entries" }
if (-not $injectorSummary.ok) { throw "Injector summary reported ok=false: $($injectorSummary.error)" }

Write-Host "[dwrt-live-server] host summary -> $hostSummaryPath"
Write-Host "[dwrt-live-server] injector summary -> $injectorSummaryPath"
Write-Host "[dwrt-live-server] OK"
