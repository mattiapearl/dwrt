param(
    [string]$Configuration = "release",

    [switch]$NoProfile,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$targetDir = Join-Path $repo "target\$Configuration"
$dllPath = Join-Path $targetDir "dwrt_runtime.dll"
$headerDir = Join-Path $repo "crates\dwrt-runtime\include"
$shimDir = Join-Path $repo "native\dwrt-shim"
$outDir = Join-Path $repo "target\shim"
$outExe = Join-Path $outDir "dwrt_shadow_smoke.exe"
$objDir = Join-Path $outDir "obj"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-shim-smoke"
}
$profileDir = Join-Path $OutputDir "profile"

New-Item -ItemType Directory -Force $outDir | Out-Null
New-Item -ItemType Directory -Force $objDir | Out-Null
New-Item -ItemType Directory -Force $OutputDir | Out-Null

Write-Host "[dwrt-shim-smoke] cargo build -p dwrt-runtime --$Configuration"
cargo build -p dwrt-runtime --$Configuration
if ($LASTEXITCODE -ne 0) { throw "cargo build failed with exit code $LASTEXITCODE" }
if (!(Test-Path $dllPath)) { throw "Missing runtime DLL: $dllPath" }

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if (!(Test-Path $vsDevCmd)) {
    $vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
}
if (!(Test-Path $vsDevCmd)) { throw "Could not find VsDevCmd.bat" }

$shimCpp = Join-Path $shimDir "dwrt_shadow_shim.cpp"
$smokeCpp = Join-Path $shimDir "smoke.cpp"
$compile = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"{1}\\" /I"{2}" /I"{3}" "{4}" "{5}" /link /OUT:"{6}"' -f $vsDevCmd, $objDir, $headerDir, $shimDir, $shimCpp, $smokeCpp, $outExe

Write-Host "[dwrt-shim-smoke] compile native shim smoke"
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "native shim smoke compile failed with exit code $LASTEXITCODE" }

if ($NoProfile) {
    Write-Warning "Running native shim smoke without profiler because -NoProfile was specified."
    & $outExe $dllPath
    if ($LASTEXITCODE -ne 0) { throw "native shim smoke failed with exit code $LASTEXITCODE" }
}
else {
    $profileArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $repo "scripts\profile-dwrt-command.ps1"),
        "-FilePath", $outExe,
        "-ArgumentLine", ('"{0}"' -f $dllPath),
        "-Name", "dwrt-shim-smoke",
        "-OutputDir", $profileDir,
        "-Profiler", $Profiler
    )
    if ($RequireProfiler) { $profileArgs += "-RequireProfiler" }
    powershell @profileArgs
    if ($LASTEXITCODE -ne 0) { throw "profiled native shim smoke failed with exit code $LASTEXITCODE" }
}

Write-Host "[dwrt-shim-smoke] OK"
