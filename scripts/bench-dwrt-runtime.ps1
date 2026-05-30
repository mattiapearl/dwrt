param(
    [UInt64]$Iterations = 5000000,

    [switch]$NoProfile,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp"
}
New-Item -ItemType Directory -Force $OutputDir | Out-Null

$reportPath = Join-Path $OutputDir "dwrt-bench.md"
$profileDir = Join-Path $OutputDir "profile"
$benchExe = Join-Path $repo "target\release\dwrt-bench.exe"

Write-Host "[dwrt-bench] cargo build -p dwrt-bench --release"
cargo build -p dwrt-bench --release
if ($LASTEXITCODE -ne 0) { throw "cargo build failed with exit code $LASTEXITCODE" }
if (!(Test-Path $benchExe)) { throw "Missing benchmark executable: $benchExe" }

$cmdLine = '"{0}" --iterations {1} > "{2}"' -f $benchExe, $Iterations, $reportPath

if ($NoProfile) {
    Write-Warning "Running benchmark without profiler because -NoProfile was specified."
    cmd.exe /c $cmdLine
    if ($LASTEXITCODE -ne 0) { throw "benchmark failed with exit code $LASTEXITCODE" }
}
else {
    $profileArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $repo "scripts\profile-dwrt-command.ps1"),
        "-FilePath", "cmd.exe",
        "-ArgumentLine", ("/c " + $cmdLine),
        "-Name", "dwrt-bench",
        "-OutputDir", $profileDir,
        "-Profiler", $Profiler
    )
    if ($RequireProfiler) { $profileArgs += "-RequireProfiler" }
    powershell @profileArgs
    if ($LASTEXITCODE -ne 0) { throw "profiled benchmark failed with exit code $LASTEXITCODE" }
}

Write-Host "[dwrt-bench] report -> $reportPath"
Get-Content $reportPath
