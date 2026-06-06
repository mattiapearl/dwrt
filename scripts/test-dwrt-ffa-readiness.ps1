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

    [int]$HoldSeconds = 5,

    [uint32]$ProbeMountMask = 7,

    [int]$TimeoutSeconds = 120,

    [switch]$KillExisting
)

$ErrorActionPreference = "Stop"

if ($RequireProfiler -and ($NoProfile -or $Profiler -eq "None")) {
    throw "-RequireProfiler cannot be combined with -NoProfile or -Profiler None."
}

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-dwrt-ffa-readiness-test"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repo $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null

$summaryPath = Join-Path $OutputDir "dwrt-ffa-readiness-test.json"
$reportPath = Join-Path $OutputDir "dwrt-ffa-readiness-test.md"
$manualMatrixPath = Join-Path $OutputDir "dwrt-ffa-manual-matrix.md"
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

    Write-Host "[dwrt-ffa-test] START $Name"
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
    Write-Host "[dwrt-ffa-test] $($status.ToUpperInvariant()) $Name"

    if ($status -ne "passed") {
        throw "$Name failed: $errorMessage"
    }
}

function New-ChildRunDir([string]$Name) {
    $path = Join-Path $OutputDir $Name
    New-Item -ItemType Directory -Force $path | Out-Null
    return $path
}

function Assert-JsonGate([string]$Path, [scriptblock]$Predicate, [string]$Message) {
    if (!(Test-Path $Path)) { throw "Missing JSON artifact: $Path" }
    $json = Get-Content -Raw $Path | ConvertFrom-Json
    if (-not (& $Predicate $json)) { throw $Message }
    return $json
}

function Write-ManualMatrix([string]$Path) {
    $md = New-Object System.Text.StringBuilder
    [void]$md.AppendLine("# DWRT FFA manual probe matrix")
    [void]$md.AppendLine("")
    [void]$md.AppendLine("Generated: " + (Get-Date).ToString("o"))
    [void]$md.AppendLine("")
    [void]$md.AppendLine('Run each scenario with a connected client. Stop detached sessions by writing the run directory''s `stop-session.flag`.')
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## Baseline target probe")
    [void]$md.AppendLine("")
    [void]$md.AppendLine('```powershell')
    [void]$md.AppendLine("scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -PollSeconds 2 -ProbeMountMask 7")
    [void]$md.AppendLine('```')
    [void]$md.AppendLine("")
    [void]$md.AppendLine("Actions: enemy player/objective, same-team player/objective, neutral, MidBoss.")
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## mp_friendlyfire comparison")
    [void]$md.AppendLine("")
    [void]$md.AppendLine('Repeat baseline with server args adding `+mp_friendlyfire 0`, then `+mp_friendlyfire 1`.')
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## Source team spoof proof")
    [void]$md.AppendLine("")
    [void]$md.AppendLine('```powershell')
    [void]$md.AppendLine("scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -TargetSourceTeamSpoofExperiment -PollSeconds 2 -ProbeMountMask 7")
    [void]$md.AppendLine('```')
    [void]$md.AppendLine("")
    [void]$md.AppendLine('Pass evidence: `sourceSpoofApplied == sourceSpoofRestored`; compare allowed/denied counters with baseline.')
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## Objective spoof regression guard")
    [void]$md.AppendLine("")
    [void]$md.AppendLine('```powershell')
    [void]$md.AppendLine("scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -FriendlyFireExperiment -FriendlyFireLocalTeam 2 -PollSeconds 2 -ProbeMountMask 7")
    [void]$md.AppendLine("scripts/start-dwrt-manual-probe-session.ps1 -NoProfile -Detached -FriendlyFireExperiment -FriendlyFireLocalTeam 3 -PollSeconds 2 -ProbeMountMask 7")
    [void]$md.AppendLine('```')
    [void]$md.AppendLine("")
    [void]$md.AppendLine('Pass evidence: enemy objective damage still reaches `TakeDamageOld`; own-objective behavior is explained by target-filter counters.')
    [void]$md.AppendLine("")
    [void]$md.AppendLine("## Bot harness")
    [void]$md.AppendLine("")
    [void]$md.AppendLine("Use shipped bot configs before custom spawning:")
    [void]$md.AppendLine("")
    [void]$md.AppendLine('```txt')
    [void]$md.AppendLine("+exec citadel_botmatch_practice_1v1.cfg")
    [void]$md.AppendLine("+exec citadel_botmatch_practice_2v2_guided.cfg")
    [void]$md.AppendLine('```')
    [void]$md.AppendLine("")
    [void]$md.AppendLine("Pass evidence: bot attacks create player/objective target-filter and damage counters without server/client crash.")
    $md.ToString() | Set-Content -Encoding UTF8 $Path
}

Invoke-LoggedStep -Name "ffa-policy-tests" -Body {
    Push-Location $repo
    try { cargo test -p dwrt-engine ffa }
    finally { Pop-Location }
}

if (-not $SkipClippy) {
    Invoke-LoggedStep -Name "ffa-policy-clippy" -Body {
        Push-Location $repo
        try { cargo clippy -p dwrt-engine --all-targets -- -D warnings }
        finally { Pop-Location }
    }
}

$hostRunDir = New-ChildRunDir "host-target-filter-signatures"
$hostArtifacts = @{
    outputDir = $hostRunDir
    bootstrapSummary = (Join-Path $hostRunDir "dwrt-host-bootstrap.json")
    resolverSummary = (Join-Path $hostRunDir "dwrt-host-smoke.json")
}
Invoke-LoggedStep -Name "target-filter-signature-smoke" -Artifacts $hostArtifacts -Body {
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
$hostSummary = Assert-JsonGate $hostArtifacts.resolverSummary {
    param($j)
    $filter = @($j.signatures | Where-Object { $_.name -eq "CitadelTargetFilter::FriendlyFire" })[0]
    $caller = @($j.signatures | Where-Object { $_.name -eq "CitadelTargetFilter::FriendlyFireCaller" })[0]
    $j.ok -and $j.requiredFailures -eq 0 -and $j.mappedRequiredFailures -eq 0 -and
        $filter.unique -and $filter.expectedRvaOk -and $filter.rva -eq "0x18d9180" -and
        $caller.unique -and $caller.expectedRvaOk -and $caller.rva -eq "0x7839d0"
} "Target-filter signatures failed FFA readiness gates"
$null = $hostSummary

if ($IncludeLiveServer) {
    $liveRunDir = New-ChildRunDir "live-target-filter-bootstrap"
    $liveArtifacts = @{
        outputDir = $liveRunDir
        hostSummary = (Join-Path $liveRunDir "dwrt-live-host.json")
        injectorSummary = (Join-Path $liveRunDir "dwrt-live-injector.json")
    }
    Invoke-LoggedStep -Name "live-target-filter-bootstrap" -Artifacts $liveArtifacts -Body {
        $liveParams = @{
            Configuration = $Configuration
            OutputDir = $liveRunDir
            ServerExe = $ServerExe
            ServerDll = $ServerDll
            ServerArgs = $ServerArgs
            WaitServerModuleSeconds = $WaitServerModuleSeconds
            HoldSeconds = $HoldSeconds
            ProbeMountMask = $ProbeMountMask
            TimeoutSeconds = $TimeoutSeconds
            Profiler = $Profiler
            XperfPreset = $XperfPreset
        }
        if ($NoProfile) { $liveParams.NoProfile = $true }
        if ($RequireProfiler) { $liveParams.RequireProfiler = $true }
        if ($KillExisting) { $liveParams.KillExisting = $true }
        & (Join-Path $repo "scripts\smoke-dwrt-live-server.ps1") @liveParams
    }
    Assert-JsonGate $liveArtifacts.injectorSummary {
        param($j) $j.ok -and $j.snapshot.usedLiveServerModule -eq 1 -and $j.snapshot.usedMappedFileFallback -eq 0
    } "Live target-filter bootstrap failed FFA readiness gates" | Out-Null
}

if ($IncludeHookInstall) {
    $hookRunDir = New-ChildRunDir "live-target-filter-hook-install"
    $hookArtifacts = @{
        outputDir = $hookRunDir
        hostSummary = (Join-Path $hookRunDir "dwrt-live-host.json")
        injectorSummary = (Join-Path $hookRunDir "dwrt-live-injector.json")
    }
    Invoke-LoggedStep -Name "live-target-filter-hook-install" -Artifacts $hookArtifacts -Body {
        $hookParams = @{
            Configuration = $Configuration
            OutputDir = $hookRunDir
            ServerExe = $ServerExe
            ServerDll = $ServerDll
            ServerArgs = $ServerArgs
            WaitServerModuleSeconds = $WaitServerModuleSeconds
            HoldSeconds = $HoldSeconds
            ProbeMountMask = $ProbeMountMask
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
    Assert-JsonGate $hookArtifacts.injectorSummary {
        param($j)
        $j.ok -and $j.installProbeHooks -and
            $j.snapshot.hookInstallAttempts -eq 7 -and
            $j.snapshot.hooksInstalled -eq 7 -and
            $j.snapshot.hookInstallFailures -eq 0 -and
            $j.targetProbeSnapshotStatus -eq 0 -and
            $j.targetProbe.enabled -eq 1
    } "Live target-filter hook install failed FFA readiness gates" | Out-Null
}

Write-ManualMatrix $manualMatrixPath

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
    probeMountMask = $ProbeMountMask
    steps = $script:Steps
    manualMatrix = $manualMatrixPath
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -Encoding UTF8 $summaryPath

$resultText = if ($ok) { "PASS" } else { "FAIL" }
$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine("# DWRT FFA readiness test")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Result: **" + $resultText + "**")
[void]$md.AppendLine("- Started: " + $script:SuiteStart.ToString("o"))
[void]$md.AppendLine("- Ended: " + $ended.ToString("o"))
[void]$md.AppendLine("- Output: " + $OutputDir)
[void]$md.AppendLine("- Manual matrix: " + $manualMatrixPath)
[void]$md.AppendLine("")
[void]$md.AppendLine("## Steps")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Step | Status | Seconds | Log |")
[void]$md.AppendLine("| --- | --- | ---: | --- |")
foreach ($step in $script:Steps) {
    [void]$md.AppendLine("| " + $step.name + " | " + $step.status + " | " + $step.elapsedSeconds + " | " + $step.logPath + " |")
}
[void]$md.AppendLine("")
[void]$md.AppendLine("## Gates")
[void]$md.AppendLine("")
[void]$md.AppendLine("- FFA policy tests: covered")
[void]$md.AppendLine("- Target-filter signature smoke: covered")
[void]$md.AppendLine("- Live server bootstrap: " + ($(if ($IncludeLiveServer) { "covered" } else { "skipped" })))
[void]$md.AppendLine("- Live hook install: " + ($(if ($IncludeHookInstall) { "covered" } else { "skipped" })))
$md.ToString() | Set-Content -Encoding UTF8 $reportPath

Write-Host "[dwrt-ffa-test] summary -> $summaryPath"
Write-Host "[dwrt-ffa-test] report -> $reportPath"
Write-Host "[dwrt-ffa-test] manual matrix -> $manualMatrixPath"
if (-not $ok) { exit 1 }
Write-Host "[dwrt-ffa-test] OK"
