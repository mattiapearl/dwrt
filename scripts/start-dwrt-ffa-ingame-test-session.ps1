param(
    [ValidateSet(
        "Baseline",
        "MpFriendlyFire0",
        "MpFriendlyFire1",
        "SourceTeamSpoof",
        "ObjectiveVictimSpoofTeam2",
        "ObjectiveVictimSpoofTeam3",
        "TargetTeamSpoofOpposing",
        "TargetTeamSpoofNeutral",
        "ForceSameTeamTargetAllow",
        "NeutralSimulation",
        "TargetBitsetAllow",
        "BotPractice1v1",
        "BotPractice2v2Guided",
        "SourceTeamSpoofBots",
        "TargetBitsetAllowBots",
        "NeutralSimulationBots"
    )]
    [string]$Profile = "Baseline",

    [switch]$ListProfiles,

    [string]$Configuration = "release",

    [string]$ServerExe = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\bin\win64\deadlock.exe",

    [int]$Port = 27068,

    [string]$Map = "dl_midtown",

    [int]$SessionSeconds = 2400,

    [int]$PollSeconds = 2,

    [uint32]$ProbeMountMask = 7,

    [string]$OutputDir = "",

    [switch]$Detached,

    [switch]$NoProfile,

    [switch]$KillExisting
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")

$profiles = [ordered]@{
    Baseline = [ordered]@{
        description = "Target/damage probes only; no FFA mutation."
        extraArgs = ""
        targetSourceSpoof = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    MpFriendlyFire0 = [ordered]@{
        description = "Explicit mp_friendlyfire 0 baseline."
        extraArgs = "+mp_friendlyfire 0"
        targetSourceSpoof = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    MpFriendlyFire1 = [ordered]@{
        description = "Engine ConVar-only friendly fire probe."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    SourceTeamSpoof = [ordered]@{
        description = "Gated source-team spoof at DWRT target filter."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $true
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    ObjectiveVictimSpoofTeam2 = [ordered]@{
        description = "Gated objective victim-team spoof scoped to local team 2."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        friendlyFireExperiment = $true
        friendlyFireLocalTeam = 2
    }
    ObjectiveVictimSpoofTeam3 = [ordered]@{
        description = "Gated objective victim-team spoof scoped to local team 3."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        friendlyFireExperiment = $true
        friendlyFireLocalTeam = 3
    }
    TargetTeamSpoofOpposing = [ordered]@{
        description = "Temporarily spoof same-team target m_iTeamNum to the source-opposing team inside the target filter."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        targetTeamSpoof = $true
        targetTeamSpoofMode = "Opposing"
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    TargetTeamSpoofNeutral = [ordered]@{
        description = "Temporarily spoof same-team target m_iTeamNum to 0 inside the target filter to test raw-team neutral-ish behavior."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        targetTeamSpoof = $true
        targetTeamSpoofMode = "Neutral"
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    ForceSameTeamTargetAllow = [ordered]@{
        description = "Force same-team target-filter and caller denials to allow; probes whether this target boundary is sufficient."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        targetForceSameTeamAllow = $true
        targetForceObjectiveAllow = $false
        targetNeutralSimulation = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    NeutralSimulation = [ordered]@{
        description = "DWRT neutral-simulation attempt: force same-team/objective filter and caller denials to allow without persistent team rewrite."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        targetForceSameTeamAllow = $true
        targetForceObjectiveAllow = $true
        targetNeutralSimulation = $true
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    TargetBitsetAllow = [ordered]@{
        description = "Mutate the live target-filter entity bitset so same-team/objective targets are present before original validation."
        extraArgs = "+mp_friendlyfire 1"
        targetSourceSpoof = $false
        targetBitsetAllow = $true
        targetForceSameTeamAllow = $false
        targetForceObjectiveAllow = $false
        targetNeutralSimulation = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    BotPractice1v1 = [ordered]@{
        description = "Shipped 1v1 practice bot harness."
        extraArgs = "+exec citadel_botmatch_practice_1v1.cfg"
        targetSourceSpoof = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    BotPractice2v2Guided = [ordered]@{
        description = "Shipped guided 2v2 practice bot harness."
        extraArgs = "+exec citadel_botmatch_practice_2v2_guided.cfg"
        targetSourceSpoof = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    SourceTeamSpoofBots = [ordered]@{
        description = "Guided bots plus source-team spoof target-filter experiment."
        extraArgs = "+mp_friendlyfire 1 +exec citadel_botmatch_practice_2v2_guided.cfg"
        targetSourceSpoof = $true
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    TargetBitsetAllowBots = [ordered]@{
        description = "Guided bots plus live target-filter entity bitset mutation experiment."
        extraArgs = "+mp_friendlyfire 1 +exec citadel_botmatch_practice_2v2_guided.cfg"
        targetSourceSpoof = $false
        targetBitsetAllow = $true
        targetForceSameTeamAllow = $false
        targetForceObjectiveAllow = $false
        targetNeutralSimulation = $false
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
    NeutralSimulationBots = [ordered]@{
        description = "Guided bots plus neutral-simulation target-policy force-allow experiment."
        extraArgs = "+mp_friendlyfire 1 +exec citadel_botmatch_practice_2v2_guided.cfg"
        targetSourceSpoof = $false
        targetForceSameTeamAllow = $true
        targetForceObjectiveAllow = $true
        targetNeutralSimulation = $true
        friendlyFireExperiment = $false
        friendlyFireLocalTeam = 2
    }
}

if ($ListProfiles) {
    foreach ($entry in $profiles.GetEnumerator()) {
        Write-Host ("{0}: {1}" -f $entry.Key, $entry.Value.description)
    }
    exit 0
}

$profileConfig = $profiles[$Profile]
if ($null -eq $profileConfig) {
    throw "Unknown profile: $Profile"
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-ffa-$($Profile)-ingame"
}
elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repo $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null

$baseArgs = "-dedicated -dev -insecure -allow_no_lobby_connect +tv_citadel_auto_record 0 +spec_replay_enable 0 +tv_enable 0 +citadel_upload_replay_enabled 0 +hostport $Port +map $Map"
$extraArgs = [string]$profileConfig.extraArgs
$serverArgs = if ([string]::IsNullOrWhiteSpace($extraArgs)) { $baseArgs } else { "$baseArgs $extraArgs" }

$manualScript = Join-Path $repo "scripts\start-dwrt-manual-probe-session.ps1"
$params = @{
    Configuration = $Configuration
    ServerExe = $ServerExe
    Port = $Port
    Map = $Map
    ServerArgs = $serverArgs
    SessionSeconds = $SessionSeconds
    PollSeconds = $PollSeconds
    ProbeMountMask = $ProbeMountMask
    OutputDir = $OutputDir
}
if ($Detached) { $params.Detached = $true }
if ($NoProfile) { $params.NoProfile = $true }
if ($KillExisting) { $params.KillExisting = $true }
if ([bool]$profileConfig.targetSourceSpoof) { $params.TargetSourceTeamSpoofExperiment = $true }
if ([bool]$profileConfig.targetTeamSpoof) {
    $params.TargetTeamSpoofExperiment = $true
    $params.TargetTeamSpoofMode = [string]$profileConfig.targetTeamSpoofMode
}
if ([bool]$profileConfig.targetForceSameTeamAllow) { $params.TargetForceSameTeamAllowExperiment = $true }
if ([bool]$profileConfig.targetForceObjectiveAllow) { $params.TargetForceObjectiveAllowExperiment = $true }
if ([bool]$profileConfig.targetNeutralSimulation) { $params.TargetNeutralSimulationExperiment = $true }
if ([bool]$profileConfig.targetBitsetAllow) { $params.TargetBitsetAllowExperiment = $true }
if ([bool]$profileConfig.friendlyFireExperiment) {
    $params.FriendlyFireExperiment = $true
    $params.FriendlyFireLocalTeam = [int]$profileConfig.friendlyFireLocalTeam
}

$checklistPath = Join-Path $OutputDir "ffa-ingame-checklist.md"
$matrixPath = Join-Path $repo "research\server-book\ffa-ingame-test-matrix.md"
$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine("# FFA in-game test checklist")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Profile: $Profile")
[void]$md.AppendLine("- Description: $($profileConfig.description)")
[void]$md.AppendLine("- Output: $OutputDir")
[void]$md.AppendLine("- Connect: connect 127.0.0.1:$Port")
[void]$md.AppendLine("- Matrix: $matrixPath")
[void]$md.AppendLine("")
[void]$md.AppendLine("## Server args")
[void]$md.AppendLine("")
[void]$md.AppendLine('```txt')
[void]$md.AppendLine($serverArgs)
[void]$md.AppendLine('```')
[void]$md.AppendLine("")
[void]$md.AppendLine("## Required actions for this profile")
[void]$md.AppendLine("")
switch ($Profile) {
    "Baseline" {
        [void]$md.AppendLine("- FFA-A01: P1 shoots enemy player/objective if available.")
        [void]$md.AppendLine("- FFA-A02/A05: P1 shoots same-team player/objective if available.")
        [void]$md.AppendLine("- FFA-A10/A11: P1 shoots neutral/MidBoss if reachable.")
    }
    "MpFriendlyFire0" {
        [void]$md.AppendLine('- Repeat baseline same-team shots with explicit mp_friendlyfire 0.')
        [void]$md.AppendLine("- Confirm denial counters/visible rejection baseline.")
    }
    "MpFriendlyFire1" {
        [void]$md.AppendLine('- Repeat baseline same-team shots with explicit mp_friendlyfire 1.')
        [void]$md.AppendLine("- Determine whether ConVar alone creates player/objective damage.")
    }
    "SourceTeamSpoof" {
        [void]$md.AppendLine("- FFA-A03/B03: same-team player shots.")
        [void]$md.AppendLine("- FFA-A06/B06: own-objective shots.")
        [void]$md.AppendLine('- Verify sourceSpoofApplied == sourceSpoofRestored.')
    }
    "ObjectiveVictimSpoofTeam2" { [void]$md.AppendLine("- Shoot enemy and own objectives; verify enemy objective damage does not regress; inspect team-2 spoof counters.") }
    "ObjectiveVictimSpoofTeam3" { [void]$md.AppendLine("- Shoot enemy and own objectives; verify enemy objective damage does not regress; inspect team-3 spoof counters.") }
    "TargetTeamSpoofOpposing" {
        [void]$md.AppendLine("- Shoot own troopers/player-like targets and own objectives; inspect targetSpoofApplied/restored and visible damage.")
        [void]$md.AppendLine("- Shoot enemy targets after same-team attempts to verify temporary m_iTeamNum restoration.")
    }
    "TargetTeamSpoofNeutral" {
        [void]$md.AppendLine("- Shoot own troopers/player-like targets and own objectives; this tests raw team 0 as neutral-ish, expected to fail closed if category masks dominate.")
        [void]$md.AppendLine("- Verify targetSpoofApplied == targetSpoofRestored.")
    }
    "ForceSameTeamTargetAllow" {
        [void]$md.AppendLine("- Shoot friendly troopers/player-like targets first, then own objectives, then enemy targets.")
        [void]$md.AppendLine("- Inspect filterForcedAllowed/callerForcedAllowed and damageSeen deltas.")
    }
    "NeutralSimulation" {
        [void]$md.AppendLine("- Treat this as the aggressive FFA probe: shoot same-team troopers/player-like targets, own shrine/Guardian/Walker, enemy targets, neutral/MidBoss if reachable.")
        [void]$md.AppendLine("- Record whether any friendly target becomes actually damageable, not just target-filter allowed.")
    }
    "TargetBitsetAllow" {
        [void]$md.AppendLine("- Shoot friendly troopers/player-like targets, own objectives, and enemy targets.")
        [void]$md.AppendLine("- Inspect bitsetAllowApplied and whether original filter allows without forced return.")
    }
    "BotPractice1v1" { [void]$md.AppendLine("- Verify bots spawn and stock bot combat produces target/damage evidence.") }
    "BotPractice2v2Guided" { [void]$md.AppendLine("- Verify guided bots spawn, lane, attack, and interact with objectives.") }
    "SourceTeamSpoofBots" { [void]$md.AppendLine("- Verify bot same-team/enemy targeting under source-team spoof; inspect damage/target counters.") }
    "TargetBitsetAllowBots" {
        [void]$md.AppendLine("- Verify bots spawn; shoot friendly bot/troopers/objectives and enemy targets.")
        [void]$md.AppendLine("- Inspect bitsetAllowApplied/classifierCalls and visible damage.")
    }
    "NeutralSimulationBots" {
        [void]$md.AppendLine("- Verify bots spawn, enemy bots still damage the player, and friendly bots/troopers remain or stop being protected.")
        [void]$md.AppendLine("- Inspect forced allow counters plus damageSeen; this is the best bot/player-like FFA probe.")
    }
}
[void]$md.AppendLine("")
[void]$md.AppendLine("## Fill after run")
[void]$md.AppendLine("")
[void]$md.AppendLine('```txt')
[void]$md.AppendLine("visible result:")
[void]$md.AppendLine("target counters:")
[void]$md.AppendLine("damage counters:")
[void]$md.AppendLine("attribution/score result:")
[void]$md.AppendLine("UI/map result:")
[void]$md.AppendLine("pass/fail/blocked:")
[void]$md.AppendLine("notes:")
[void]$md.AppendLine('```')
$md.ToString() | Set-Content -Encoding UTF8 $checklistPath

Write-Host "[dwrt-ffa-ingame] profile=$Profile"
Write-Host "[dwrt-ffa-ingame] output -> $OutputDir"
Write-Host "[dwrt-ffa-ingame] checklist -> $checklistPath"
Write-Host "[dwrt-ffa-ingame] matrix -> $matrixPath"
Write-Host "[dwrt-ffa-ingame] connect with: connect 127.0.0.1:$Port"

& $manualScript @params
