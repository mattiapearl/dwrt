param(
    [string]$DeadworksExe = "C:\Code\deadworks-tournament\deadworks\x64\Release\deadworks.exe",

    [string]$RuntimeDll = "",

    [int]$TimeoutSeconds = 45,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [string]$OutputDir = "",

    [uint32]$UsercmdMountMask = 0,

    [int]$NetOutgoingSerializedId = -1,

    [int]$NetIncomingSerializedId = -1,

    [string]$ExtraServerArgs = "-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 +spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 +hostport 27067 +map dl_midtown",

    [switch]$KillExisting
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($RuntimeDll)) {
    $RuntimeDll = Join-Path $repo "target\release\dwrt_runtime.dll"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-deadworks-shadow-server"
}
$profileDir = Join-Path $OutputDir "profile"
$logPath = Join-Path $OutputDir "server.log"

New-Item -ItemType Directory -Force $OutputDir | Out-Null

Write-Host "[dwrt-deadworks-smoke] cargo build -p dwrt-runtime --release"
cargo build -p dwrt-runtime --release
if ($LASTEXITCODE -ne 0) { throw "cargo build failed with exit code $LASTEXITCODE" }

if (!(Test-Path $DeadworksExe)) { throw "Missing Deadworks executable: $DeadworksExe" }
if (!(Test-Path $RuntimeDll)) { throw "Missing DWRT runtime DLL: $RuntimeDll" }

$existing = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $DeadworksExe })
if ($existing.Count -gt 0) {
    if (-not $KillExisting) {
        $ids = ($existing | ForEach-Object { $_.Id }) -join ", "
        throw "Existing Deadworks smoke process(es) for '$DeadworksExe' are still running: $ids. Re-run with -KillExisting or stop them first."
    }
    Write-Warning "Stopping existing Deadworks smoke process(es): $($existing.Id -join ', ')"
    $existing | Stop-Process -Force
    Start-Sleep -Seconds 1
}

$oldEnable = $env:DWRT_SHADOW_ENABLE
$oldRuntime = $env:DWRT_RUNTIME_DLL
$oldTiming = $env:DWRT_SHADOW_TIMING
$oldLogInterval = $env:DWRT_SHADOW_LOG_INTERVAL_MS
$oldSlow = $env:DWRT_SHADOW_SLOW_THRESHOLD_NS
$oldUsercmd = $env:DWRT_SHADOW_USERCMD_MOUNT_MASK
$oldOut = $env:DWRT_SHADOW_NET_OUT_SERIALIZED_ID
$oldIn = $env:DWRT_SHADOW_NET_IN_SERIALIZED_ID

try {
    $env:DWRT_SHADOW_ENABLE = "1"
    $env:DWRT_RUNTIME_DLL = $RuntimeDll
    $env:DWRT_SHADOW_TIMING = "1"
    $env:DWRT_SHADOW_LOG_INTERVAL_MS = "5000"
    $env:DWRT_SHADOW_SLOW_THRESHOLD_NS = "100000"
    $env:DWRT_SHADOW_USERCMD_MOUNT_MASK = $UsercmdMountMask.ToString()
    if ($NetOutgoingSerializedId -ge 0) { $env:DWRT_SHADOW_NET_OUT_SERIALIZED_ID = $NetOutgoingSerializedId.ToString() }
    if ($NetIncomingSerializedId -ge 0) { $env:DWRT_SHADOW_NET_IN_SERIALIZED_ID = $NetIncomingSerializedId.ToString() }

    $deadworksDir = Split-Path -Parent $DeadworksExe
    $serverArgs = $ExtraServerArgs
    $cmdLine = 'cd /d "{0}" && "{1}" {2} > "{3}" 2>&1' -f $deadworksDir, $DeadworksExe, $serverArgs, $logPath

    $profileScript = Join-Path $repo "scripts\profile-dwrt-command.ps1"
    $profileArgs = @{
        FilePath = "cmd.exe"
        ArgumentLine = ("/c " + $cmdLine)
        Name = "deadworks-shadow-server"
        OutputDir = $profileDir
        Profiler = $Profiler
        TimeoutSeconds = $TimeoutSeconds
        AllowTimeout = $true
    }
    if ($RequireProfiler) { $profileArgs.RequireProfiler = $true }

    & $profileScript @profileArgs
    if ($LASTEXITCODE -ne 0) { throw "profiled Deadworks shadow smoke failed with exit code $LASTEXITCODE" }
}
finally {
    $env:DWRT_SHADOW_ENABLE = $oldEnable
    $env:DWRT_RUNTIME_DLL = $oldRuntime
    $env:DWRT_SHADOW_TIMING = $oldTiming
    $env:DWRT_SHADOW_LOG_INTERVAL_MS = $oldLogInterval
    $env:DWRT_SHADOW_SLOW_THRESHOLD_NS = $oldSlow
    $env:DWRT_SHADOW_USERCMD_MOUNT_MASK = $oldUsercmd
    $env:DWRT_SHADOW_NET_OUT_SERIALIZED_ID = $oldOut
    $env:DWRT_SHADOW_NET_IN_SERIALIZED_ID = $oldIn
}

Write-Host "[dwrt-deadworks-smoke] server log -> $logPath"
if (Test-Path $logPath) {
    Select-String -Path $logPath -Pattern "\[DWRT\]|shadow|error|failed" -CaseSensitive:$false | Select-Object -First 120 | ForEach-Object { $_.Line }
}
Write-Host "[dwrt-deadworks-smoke] OK"
