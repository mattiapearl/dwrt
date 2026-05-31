param(
    [string]$Configuration = "release",

    [switch]$SkipClippy,

    [switch]$IncludeLiveServer,

    [switch]$IncludeHookInstall,

    [switch]$NoProfile,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [ValidateSet("Cpu", "Latency")]
    [string]$XperfPreset = "Latency",

    [string]$OutputDir = "",

    [string]$ServerExe = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadlock.exe",

    [string]$ServerDll = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\bin\win64\server.dll",

    [string]$ServerArgs = "-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 +spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 +hostport 27068 +map dl_midtown",

    [int]$WaitServerModuleSeconds = 45,

    [int]$HoldSeconds = 3,

    [int]$TimeoutSeconds = 90,

    [switch]$KillExisting
)

$ErrorActionPreference = "Stop"

if ($RequireProfiler -and ($NoProfile -or $Profiler -eq "None")) {
    throw "-RequireProfiler cannot be combined with -NoProfile or -Profiler None."
}

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-dwrt-native-stack-test"
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null

$summaryPath = Join-Path $OutputDir "dwrt-native-stack-test.json"
$reportPath = Join-Path $OutputDir "dwrt-native-stack-test.md"
$logsDir = Join-Path $OutputDir "logs"
New-Item -ItemType Directory -Force $logsDir | Out-Null

$script:Steps = New-Object System.Collections.Generic.List[object]
$script:SuiteStart = Get-Date

function ConvertTo-SafeName([string]$Name) {
    return ($Name -replace '[^A-Za-z0-9_.-]', '_')
}

function Add-StepResult {
    param(
        [string]$Name,
        [string]$Status,
        [datetime]$StartedAt,
        [datetime]$EndedAt,
        [int]$ExitCode,
        [string]$LogPath,
        [string]$ErrorMessage = "",
        [hashtable]$Artifacts = @{}
    )
    $script:Steps.Add([ordered]@{
        name = $Name
        status = $Status
        startedAt = $StartedAt.ToString("o")
        endedAt = $EndedAt.ToString("o")
        elapsedSeconds = [math]::Round(($EndedAt - $StartedAt).TotalSeconds, 6)
        exitCode = $ExitCode
        logPath = $LogPath
        error = $ErrorMessage
        artifacts = $Artifacts
    }) | Out-Null
}

function Invoke-LoggedStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Body,

        [hashtable]$Artifacts = @{}
    )

    $safeName = ConvertTo-SafeName $Name
    $logPath = Join-Path $logsDir "$safeName.log"
    $started = Get-Date
    $status = "passed"
    $exitCode = 0
    $errorMessage = ""

    Write-Host "[dwrt-stack-test] START $Name"
    $transcriptStarted = $false
    try {
        Start-Transcript -Path $logPath -Force | Out-Null
        $transcriptStarted = $true
        & $Body
        if ($LASTEXITCODE -ne 0) {
            $exitCode = $LASTEXITCODE
            throw "$Name exited with code $exitCode"
        }
    }
    catch {
        $status = "failed"
        if ($exitCode -eq 0) { $exitCode = 1 }
        $errorMessage = $_.Exception.Message
        Write-Host "ERROR: $errorMessage"
    }
    finally {
        if ($transcriptStarted) {
            try { Stop-Transcript | Out-Null } catch { }
        }
    }

    $ended = Get-Date
    Add-StepResult -Name $Name -Status $status -StartedAt $started -EndedAt $ended -ExitCode $exitCode -LogPath $logPath -ErrorMessage $errorMessage -Artifacts $Artifacts
    Write-Host "[dwrt-stack-test] $($status.ToUpperInvariant()) $Name"

    if ($status -ne "passed") {
        throw "$Name failed: $errorMessage"
    }
}

function New-ChildRunDir([string]$Name) {
    $path = Join-Path $OutputDir $Name
    New-Item -ItemType Directory -Force $path | Out-Null
    return $path
}

function Get-ProfileJsons([string]$RunDir) {
    $profileDir = Join-Path $RunDir "profile"
    if (!(Test-Path $profileDir)) { return @() }
    return @(Get-ChildItem -Path $profileDir -Filter "*.json" -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
}

function Assert-JsonGate([string]$Path, [scriptblock]$Predicate, [string]$Message) {
    if (!(Test-Path $Path)) { throw "Missing JSON artifact: $Path" }
    $json = Get-Content -Raw $Path | ConvertFrom-Json
    if (-not (& $Predicate $json)) { throw $Message }
    return $json
}

$runtimeSmokeArtifacts = @{}
Invoke-LoggedStep -Name "cargo-test-workspace" -Body {
    Push-Location $repo
    try { cargo test --workspace }
    finally { Pop-Location }
}

if (-not $SkipClippy) {
    Invoke-LoggedStep -Name "cargo-clippy-workspace" -Body {
        Push-Location $repo
        try { cargo clippy --workspace --all-targets -- -D warnings }
        finally { Pop-Location }
    }
}

Invoke-LoggedStep -Name "runtime-c-abi-smoke" -Body {
    & (Join-Path $repo "scripts\smoke-dwrt-runtime.ps1") -Configuration $Configuration
}

$hostRunDir = New-ChildRunDir "host-resolver"
$hostArtifacts = @{
    outputDir = $hostRunDir
    bootstrapSummary = (Join-Path $hostRunDir "dwrt-host-bootstrap.json")
    resolverSummary = (Join-Path $hostRunDir "dwrt-host-smoke.json")
}
Invoke-LoggedStep -Name "host-resolver-smoke" -Artifacts $hostArtifacts -Body {
    $hostParams = @{
        Configuration = $Configuration
        OutputDir = $hostRunDir
        MappedModuleCheck = $true
        Profiler = $Profiler
        XperfPreset = $XperfPreset
    }
    if ($NoProfile) { $hostParams.NoProfile = $true }
    if ($RequireProfiler) { $hostParams.RequireProfiler = $true }
    & (Join-Path $repo "scripts\smoke-dwrt-host.ps1") @hostParams
}
$hostResolver = Assert-JsonGate $hostArtifacts.resolverSummary { param($j) $j.ok -and $j.requiredFailures -eq 0 -and $j.mappedRequiredFailures -eq 0 -and $j.runtime.probeOk } "Host resolver smoke gates failed"
$hostBootstrap = Assert-JsonGate $hostArtifacts.bootstrapSummary { param($j) $j.initialized -and $j.runtimeLoaded -and $j.runtimeProbeOk -and $j.signaturesChecked -and $j.signatureRequiredFailures -eq 0 } "Host bootstrap smoke gates failed"
$hostArtifacts.profileJsons = @(Get-ProfileJsons $hostRunDir)

if ($IncludeLiveServer) {
    $liveRunDir = New-ChildRunDir "live-server-bootstrap"
    $liveArtifacts = @{
        outputDir = $liveRunDir
        hostSummary = (Join-Path $liveRunDir "dwrt-live-host.json")
        injectorSummary = (Join-Path $liveRunDir "dwrt-live-injector.json")
    }
    Invoke-LoggedStep -Name "live-server-bootstrap-smoke" -Artifacts $liveArtifacts -Body {
        $liveParams = @{
            Configuration = $Configuration
            OutputDir = $liveRunDir
            ServerExe = $ServerExe
            ServerDll = $ServerDll
            ServerArgs = $ServerArgs
            WaitServerModuleSeconds = $WaitServerModuleSeconds
            HoldSeconds = $HoldSeconds
            TimeoutSeconds = $TimeoutSeconds
            Profiler = $Profiler
            XperfPreset = $XperfPreset
        }
        if ($NoProfile) { $liveParams.NoProfile = $true }
        if ($RequireProfiler) { $liveParams.RequireProfiler = $true }
        if ($KillExisting) { $liveParams.KillExisting = $true }
        & (Join-Path $repo "scripts\smoke-dwrt-live-server.ps1") @liveParams
    }
    $liveHost = Assert-JsonGate $liveArtifacts.hostSummary { param($j) $j.initialized -and $j.runtimeLoaded -and $j.runtimeProbeOk -and $j.signaturesChecked -and $j.usedLiveServerModule -and (-not $j.usedMappedFileFallback) -and $j.signatureRequiredFailures -eq 0 -and $j.hookInstallAttempts -eq 0 -and $j.testpoints.initializeCalls -eq 1 -and $j.testpoints.callbackRecursiveEntries -eq 0 } "Live server bootstrap gates failed"
    $liveInjector = Assert-JsonGate $liveArtifacts.injectorSummary { param($j) $j.ok -and $j.snapshot.usedLiveServerModule -eq 1 -and $j.snapshot.usedMappedFileFallback -eq 0 } "Live server injector gates failed"
    $liveArtifacts.profileJsons = @(Get-ProfileJsons $liveRunDir)
}

if ($IncludeHookInstall) {
    if (-not $IncludeLiveServer) {
        Write-Warning "-IncludeHookInstall implies a live server run. Running hook install smoke."
    }
    $hookRunDir = New-ChildRunDir "live-hook-install"
    $hookArtifacts = @{
        outputDir = $hookRunDir
        hostSummary = (Join-Path $hookRunDir "dwrt-live-host.json")
        injectorSummary = (Join-Path $hookRunDir "dwrt-live-injector.json")
    }
    Invoke-LoggedStep -Name "live-hook-install-smoke" -Artifacts $hookArtifacts -Body {
        $hookParams = @{
            Configuration = $Configuration
            OutputDir = $hookRunDir
            ServerExe = $ServerExe
            ServerDll = $ServerDll
            ServerArgs = $ServerArgs
            WaitServerModuleSeconds = $WaitServerModuleSeconds
            HoldSeconds = $HoldSeconds
            TimeoutSeconds = $TimeoutSeconds
            Profiler = $Profiler
            XperfPreset = $XperfPreset
            InstallProbeHooks = $true
        }
        if ($NoProfile) { $hookParams.NoProfile = $true }
        if ($RequireProfiler) { $hookParams.RequireProfiler = $true }
        if ($KillExisting) { $hookParams.KillExisting = $true }
        & (Join-Path $repo "scripts\smoke-dwrt-live-server.ps1") @hookParams
    }
    $hookHost = Assert-JsonGate $hookArtifacts.hostSummary { param($j) $j.initialized -and $j.runtimeLoaded -and $j.runtimeProbeOk -and $j.signaturesChecked -and $j.usedLiveServerModule -and (-not $j.usedMappedFileFallback) -and $j.signatureRequiredFailures -eq 0 -and $j.hookInstallAttempts -eq 3 -and $j.hooksInstalled -eq 3 -and $j.hookInstallFailures -eq 0 -and $j.testpoints.initializeCalls -eq 1 -and $j.testpoints.callbackRecursiveEntries -eq 0 } "Live hook install gates failed"
    $hookInjector = Assert-JsonGate $hookArtifacts.injectorSummary { param($j) $j.ok -and $j.installProbeHooks -and $j.snapshot.hooksInstalled -eq 3 -and $j.snapshot.hookInstallFailures -eq 0 } "Live hook injector gates failed"
    $hookArtifacts.profileJsons = @(Get-ProfileJsons $hookRunDir)
}

$ended = Get-Date
$failedSteps = @($script:Steps | Where-Object { $_.status -ne "passed" })
$ok = $failedSteps.Count -eq 0

$summary = [ordered]@{
    ok = $ok
    startedAt = $script:SuiteStart.ToString("o")
    endedAt = $ended.ToString("o")
    elapsedSeconds = [math]::Round(($ended - $script:SuiteStart).TotalSeconds, 6)
    outputDir = $OutputDir
    configuration = $Configuration
    includeLiveServer = [bool]$IncludeLiveServer
    includeHookInstall = [bool]$IncludeHookInstall
    noProfile = [bool]$NoProfile
    profiler = $(if ($NoProfile) { "None" } else { $Profiler })
    xperfPreset = $XperfPreset
    steps = $script:Steps
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 $summaryPath

$md = New-Object System.Text.StringBuilder
$resultText = if ($ok) { "PASS" } else { "FAIL" }
$profileText = if ($NoProfile) { "None" } else { $Profiler }
$clippyText = if ($SkipClippy) { "tests only; clippy skipped" } else { "covered" }
$liveText = if ($IncludeLiveServer) { "covered" } else { "skipped" }
$hookText = if ($IncludeHookInstall) { "covered" } else { "skipped" }
[void]$md.AppendLine("# DWRT native stack test")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Result: **" + $resultText + "**")
[void]$md.AppendLine("- Started: " + $script:SuiteStart.ToString("o"))
[void]$md.AppendLine("- Ended: " + $ended.ToString("o"))
[void]$md.AppendLine("- Output: " + $OutputDir)
[void]$md.AppendLine("- Profiling: " + $profileText + " / " + $XperfPreset)
[void]$md.AppendLine("")
[void]$md.AppendLine("## Steps")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Step | Status | Seconds | Log |")
[void]$md.AppendLine("| --- | --- | ---: | --- |")
foreach ($step in $script:Steps) {
    [void]$md.AppendLine("| " + $step.name + " | " + $step.status + " | " + $step.elapsedSeconds + " | " + $step.logPath + " |")
}
[void]$md.AppendLine("")
[void]$md.AppendLine("## Key gates")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Rust workspace tests and clippy: " + $clippyText)
[void]$md.AppendLine("- Runtime C ABI smoke: covered")
[void]$md.AppendLine("- Host resolver/bootstrap smoke: covered")
[void]$md.AppendLine("- Live server bootstrap: " + $liveText)
[void]$md.AppendLine("- Live hook install: " + $hookText)
[void]$md.AppendLine("")
[void]$md.AppendLine("## Debug artifacts")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Suite JSON: " + $summaryPath)
[void]$md.AppendLine("- Per-step logs: " + $logsDir)
[void]$md.AppendLine("- Child run directories are under: " + $OutputDir)
[void]$md.AppendLine("- ETW .etl files are intentionally under ignored run directories and should not be committed.")
$md.ToString() | Set-Content -Encoding UTF8 $reportPath

Write-Host "[dwrt-stack-test] summary -> $summaryPath"
Write-Host "[dwrt-stack-test] report -> $reportPath"
if (-not $ok) { exit 1 }
Write-Host "[dwrt-stack-test] OK"
