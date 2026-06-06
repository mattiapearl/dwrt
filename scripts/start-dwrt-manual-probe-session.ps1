param(
    [string]$Configuration = "release",

    [string]$ServerExe = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadlock.exe",

    [string]$ServerDll = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\bin\win64\server.dll",

    [int]$Port = 27068,

    [string]$Map = "dl_midtown",

    [string]$ServerArgs = "",

    [int]$SessionSeconds = 2400,

    [int]$PollSeconds = 5,

    [uint32]$ProbeMountMask = 7,

    [int]$WaitServerModuleSeconds = 60,

    [int]$TimeoutSeconds = 0,

    [switch]$NoProfile,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [ValidateSet("Cpu", "Latency")]
    [string]$XperfPreset = "Latency",

    [string]$OutputDir = "",

    [switch]$Detached,

    [switch]$StrictNoRecursiveCallbacks,

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

if ($RequireProfiler -and ($NoProfile -or $Profiler -eq "None")) {
    throw "-RequireProfiler cannot be combined with -NoProfile or -Profiler None."
}

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-dwrt-manual-probe-session"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repo $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null

if ([string]::IsNullOrWhiteSpace($ServerArgs)) {
    $ServerArgs = "-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 +spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 +hostport $Port +map $Map"
}
if ($TimeoutSeconds -le 0) {
    $TimeoutSeconds = $SessionSeconds + 120
}

$snapshotJsonl = Join-Path $OutputDir "dwrt-manual-probe-snapshots.jsonl"
$hostSummary = Join-Path $OutputDir "dwrt-live-host.json"
$injectorSummary = Join-Path $OutputDir "dwrt-live-injector.json"
$stopFile = Join-Path $OutputDir "stop-session.flag"
$sessionLog = Join-Path $OutputDir "manual-probe-session.log"
$sessionErr = Join-Path $OutputDir "manual-probe-session.err.log"
$runnerPath = Join-Path $OutputDir "run-manual-probe-session.ps1"
$sessionInfoPath = Join-Path $OutputDir "dwrt-manual-probe-session.json"

if (Test-Path $stopFile) { Remove-Item -Force $stopFile }
if (Test-Path $snapshotJsonl) { Remove-Item -Force $snapshotJsonl }

$smokeScript = Join-Path $repo "scripts\smoke-dwrt-live-server.ps1"
$runnerLines = New-Object System.Collections.Generic.List[string]
$runnerLines.Add('$ErrorActionPreference = "Stop"') | Out-Null
$runnerLines.Add('Set-Location "' + ($repo -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('$params = @{') | Out-Null
$runnerLines.Add('  Configuration = "' + ($Configuration -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  ServerExe = "' + ($ServerExe -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  ServerDll = "' + ($ServerDll -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  ServerArgs = "' + ($ServerArgs -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  WaitServerModuleSeconds = ' + $WaitServerModuleSeconds) | Out-Null
$runnerLines.Add('  HoldSeconds = ' + $SessionSeconds) | Out-Null
$runnerLines.Add('  PollSeconds = ' + $PollSeconds) | Out-Null
$runnerLines.Add('  ProbeMountMask = ' + $ProbeMountMask) | Out-Null
$runnerLines.Add('  TimeoutSeconds = ' + $TimeoutSeconds) | Out-Null
$runnerLines.Add('  OutputDir = "' + ($OutputDir -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  SnapshotJsonl = "' + ($snapshotJsonl -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  StopFile = "' + ($stopFile -replace '"', '`"') + '"') | Out-Null
$runnerLines.Add('  Profiler = "' + $Profiler + '"') | Out-Null
$runnerLines.Add('  XperfPreset = "' + $XperfPreset + '"') | Out-Null
$runnerLines.Add('}') | Out-Null
$runnerLines.Add('$params["InstallProbeHooks"] = $true') | Out-Null
if (-not $StrictNoRecursiveCallbacks) { $runnerLines.Add('$params["AllowRecursiveCallbacks"] = $true') | Out-Null }
if ($WalkerPatrolExperiment) {
    $runnerLines.Add('$params["WalkerPatrolExperiment"] = $true') | Out-Null
    $runnerLines.Add('$params["WalkerPatrolMode"] = "' + $WalkerPatrolMode + '"') | Out-Null
    $runnerLines.Add('$params["WalkerPatrolStride"] = ' + $WalkerPatrolStride) | Out-Null
    $runnerLines.Add('$params["WalkerPatrolVelocities"] = "' + ($WalkerPatrolVelocities -replace '"', '`"') + '"') | Out-Null
}
if ($FriendlyFireExperiment) {
    $runnerLines.Add('$params["FriendlyFireExperiment"] = $true') | Out-Null
    $runnerLines.Add('$params["FriendlyFireLocalTeam"] = ' + $FriendlyFireLocalTeam) | Out-Null
}
if ($TargetSourceTeamSpoofExperiment) { $runnerLines.Add('$params["TargetSourceTeamSpoofExperiment"] = $true') | Out-Null }
if ($TargetTeamSpoofExperiment) {
    $runnerLines.Add('$params["TargetTeamSpoofExperiment"] = $true') | Out-Null
    $runnerLines.Add('$params["TargetTeamSpoofMode"] = "' + $TargetTeamSpoofMode + '"') | Out-Null
}
if ($TargetForceSameTeamAllowExperiment) { $runnerLines.Add('$params["TargetForceSameTeamAllowExperiment"] = $true') | Out-Null }
if ($TargetForceObjectiveAllowExperiment) { $runnerLines.Add('$params["TargetForceObjectiveAllowExperiment"] = $true') | Out-Null }
if ($TargetNeutralSimulationExperiment) { $runnerLines.Add('$params["TargetNeutralSimulationExperiment"] = $true') | Out-Null }
if ($TargetBitsetAllowExperiment) { $runnerLines.Add('$params["TargetBitsetAllowExperiment"] = $true') | Out-Null }
if ($NoProfile) { $runnerLines.Add('$params["NoProfile"] = $true') | Out-Null }
if ($RequireProfiler) { $runnerLines.Add('$params["RequireProfiler"] = $true') | Out-Null }
if ($KillExisting) { $runnerLines.Add('$params["KillExisting"] = $true') | Out-Null }
$runnerLines.Add('& "' + ($smokeScript -replace '"', '`"') + '" @params') | Out-Null
$runnerLines | Set-Content -Encoding UTF8 $runnerPath

$sessionInfo = [ordered]@{
    outputDir = $OutputDir
    detached = [bool]$Detached
    serverExe = $ServerExe
    serverDll = $ServerDll
    serverArgs = $ServerArgs
    port = $Port
    map = $Map
    sessionSeconds = $SessionSeconds
    pollSeconds = $PollSeconds
    probeMountMask = $ProbeMountMask
    allowRecursiveCallbacks = -not [bool]$StrictNoRecursiveCallbacks
    walkerPatrolExperiment = [bool]$WalkerPatrolExperiment
    walkerPatrolMode = $WalkerPatrolMode
    walkerPatrolStride = $WalkerPatrolStride
    walkerPatrolVelocities = $WalkerPatrolVelocities
    friendlyFireExperiment = [bool]$FriendlyFireExperiment
    friendlyFireLocalTeam = $FriendlyFireLocalTeam
    targetSourceTeamSpoofExperiment = [bool]$TargetSourceTeamSpoofExperiment
    targetTeamSpoofExperiment = [bool]$TargetTeamSpoofExperiment
    targetTeamSpoofMode = $TargetTeamSpoofMode
    targetForceSameTeamAllowExperiment = [bool]$TargetForceSameTeamAllowExperiment
    targetForceObjectiveAllowExperiment = [bool]$TargetForceObjectiveAllowExperiment
    targetNeutralSimulationExperiment = [bool]$TargetNeutralSimulationExperiment
    targetBitsetAllowExperiment = [bool]$TargetBitsetAllowExperiment
    noProfile = [bool]$NoProfile
    profiler = $(if ($NoProfile) { "None" } else { $Profiler })
    xperfPreset = $XperfPreset
    runner = $runnerPath
    log = $sessionLog
    errorLog = $sessionErr
    snapshotJsonl = $snapshotJsonl
    hostSummary = $hostSummary
    injectorSummary = $injectorSummary
    stopFile = $stopFile
    connectCommand = "connect 127.0.0.1:$Port"
}
$sessionInfo | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $sessionInfoPath

if ($Detached) {
    $process = Start-Process -FilePath "powershell.exe" `
        -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $runnerPath) `
        -RedirectStandardOutput $sessionLog `
        -RedirectStandardError $sessionErr `
        -PassThru
    Write-Host "[dwrt-manual-probe] started detached runner pid=$($process.Id)"
    Write-Host "[dwrt-manual-probe] output -> $OutputDir"
    Write-Host "[dwrt-manual-probe] log -> $sessionLog"
    Write-Host "[dwrt-manual-probe] snapshots -> $snapshotJsonl"
    Write-Host "[dwrt-manual-probe] stop file -> $stopFile"
    Write-Host "[dwrt-manual-probe] connect with: connect 127.0.0.1:$Port"
    exit 0
}

& $runnerPath
