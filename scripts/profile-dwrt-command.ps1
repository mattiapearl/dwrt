param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,

    [string[]]$Arguments = @(),

    [string]$ArgumentLine = "",

    [string]$Name = "dwrt-profile",

    [string]$OutputDir = "",

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [switch]$RequireProfiler
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repo "target\profiles"
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$safeName = ($Name -replace '[^A-Za-z0-9_.-]', '_')
$baseName = "$timestamp-$safeName"
$etlPath = Join-Path $OutputDir "$baseName.etl"
$metaPath = Join-Path $OutputDir "$baseName.json"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

function Find-CommandPath([string]$CommandName) {
    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($null -eq $command) { return $null }
    return $command.Source
}

function Resolve-Profiler {
    param([string]$Requested)

    if ($Requested -eq "None") { return "None" }
    if (-not $isAdmin) {
        if ($RequireProfiler) {
            throw "ETW profiling requires an elevated/admin shell. Re-run as administrator or use -Profiler None."
        }
        Write-Warning "ETW profiling requires an elevated/admin shell; running without ETW profile."
        return "None"
    }

    $wpr = Find-CommandPath "wpr.exe"
    $xperf = Find-CommandPath "xperf.exe"

    if ($Requested -eq "Wpr") {
        if ($null -eq $wpr) { throw "wpr.exe not found" }
        return "Wpr"
    }
    if ($Requested -eq "Xperf") {
        if ($null -eq $xperf) { throw "xperf.exe not found" }
        return "Xperf"
    }

    # Prefer xperf when available because the explicit buffer/file settings have
    # been more reliable for short DWRT smoke runs than the default WPR CPU profile.
    if ($null -ne $xperf) { return "Xperf" }
    if ($null -ne $wpr) { return "Wpr" }
    if ($RequireProfiler) { throw "No ETW profiler found. Install Windows Performance Toolkit or use -Profiler None." }
    Write-Warning "No ETW profiler found; running without ETW profile."
    return "None"
}

function Start-Profiler([string]$Selected) {
    switch ($Selected) {
        "Wpr" {
            Write-Host "[dwrt-profile] starting WPR CPU profile"
            & wpr.exe -start CPU -filemode | Write-Host
            if ($LASTEXITCODE -ne 0) { throw "wpr start failed with exit code $LASTEXITCODE" }
        }
        "Xperf" {
            Write-Host "[dwrt-profile] starting xperf CPU profile"
            & xperf.exe -on PROC_THREAD+LOADER+PROFILE -stackwalk Profile -buffersize 1024 -MaxFile 1024 -FileMode Circular | Write-Host
            if ($LASTEXITCODE -ne 0) { throw "xperf start failed with exit code $LASTEXITCODE" }
        }
    }
}

function Stop-Profiler([string]$Selected, [string]$Path) {
    switch ($Selected) {
        "Wpr" {
            Write-Host "[dwrt-profile] stopping WPR profile -> $Path"
            & wpr.exe -stop $Path | Write-Host
            if ($LASTEXITCODE -ne 0) { throw "wpr stop failed with exit code $LASTEXITCODE" }
        }
        "Xperf" {
            Write-Host "[dwrt-profile] stopping xperf profile -> $Path"
            & xperf.exe -d $Path | Write-Host
            if ($LASTEXITCODE -ne 0) { throw "xperf stop failed with exit code $LASTEXITCODE" }
        }
    }
}

$selectedProfiler = Resolve-Profiler $Profiler
$start = Get-Date
$exitCode = $null
$stopError = $null
$startedProfiler = $false

$displayArguments = if ([string]::IsNullOrWhiteSpace($ArgumentLine)) { $Arguments -join ' ' } else { $ArgumentLine }
Write-Host "[dwrt-profile] command: $FilePath $displayArguments"
Write-Host "[dwrt-profile] profiler: $selectedProfiler"

try {
    if ($selectedProfiler -ne "None") {
        Start-Profiler $selectedProfiler
        $startedProfiler = $true
    }

    if ([string]::IsNullOrWhiteSpace($ArgumentLine)) {
        $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru
    }
    else {
        $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentLine -NoNewWindow -Wait -PassThru
    }
    $exitCode = $process.ExitCode
}
finally {
    $end = Get-Date
    if ($startedProfiler) {
        try {
            Stop-Profiler $selectedProfiler $etlPath
        }
        catch {
            $stopError = $_.Exception.Message
            Write-Warning $stopError
        }
    }

    $metadata = [ordered]@{
        name = $Name
        command = $FilePath
        arguments = $Arguments
        argumentLine = $ArgumentLine
        profiler = $selectedProfiler
        requiredProfiler = [bool]$RequireProfiler
        elevated = $isAdmin
        startedAt = $start.ToString("o")
        endedAt = $end.ToString("o")
        elapsedSeconds = [math]::Round(($end - $start).TotalSeconds, 6)
        exitCode = $exitCode
        etlPath = $(if ($startedProfiler) { $etlPath } else { $null })
        profilerStopError = $stopError
    }
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $metaPath
    Write-Host "[dwrt-profile] metadata -> $metaPath"
}

if ($null -eq $exitCode) { $exitCode = 1 }
if ($exitCode -ne 0) { exit $exitCode }
