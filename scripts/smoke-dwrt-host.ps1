param(
    [string]$Configuration = "release",

    [string]$ServerDll = "C:\Program Files (x86)\Steam\steamapps\common\Deadlock\game\citadel\bin\win64\server.dll",

    [switch]$NoProfile,

    [switch]$RequireProfiler,

    [ValidateSet("Auto", "Wpr", "Xperf", "None")]
    [string]$Profiler = "Auto",

    [ValidateSet("Cpu", "Latency")]
    [string]$XperfPreset = "Cpu",

    [string]$OutputDir = "",

    [switch]$AllowExpectedRvaDrift,

    [switch]$MappedModuleCheck
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$targetDir = Join-Path $repo "target\$Configuration"
$dllPath = Join-Path $targetDir "dwrt_runtime.dll"
$runtimeHeaderDir = Join-Path $repo "crates\dwrt-runtime\include"
$hostDir = Join-Path $repo "native\dwrt-host"
$shimDir = Join-Path $repo "native\dwrt-shim"
$vendorDir = Join-Path $repo "native\vendor"
$outDir = Join-Path $repo "target\host"
$outExe = Join-Path $outDir "dwrt_host_smoke.exe"
$hostDll = Join-Path $outDir "dwrt_host.dll"
$hostImportLib = Join-Path $outDir "dwrt_host.lib"
$bootstrapExe = Join-Path $outDir "dwrt_host_bootstrap_smoke.exe"
$testpointsExe = Join-Path $outDir "dwrt_host_testpoints_smoke.exe"
$objDir = Join-Path $outDir "obj"
$smokeObjDir = Join-Path $objDir "smoke"
$hostDllObjDir = Join-Path $objDir "hostdll"
$bootstrapObjDir = Join-Path $objDir "bootstrap"
$testpointsObjDir = Join-Path $objDir "testpoints"

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDir = Join-Path $repo "research\benchmarks\runs\$stamp-host-smoke"
}
$profileDir = Join-Path $OutputDir "profile"
$summaryPath = Join-Path $OutputDir "dwrt-host-smoke.json"
$bootstrapSummaryPath = Join-Path $OutputDir "dwrt-host-bootstrap.json"

New-Item -ItemType Directory -Force $outDir | Out-Null
New-Item -ItemType Directory -Force $objDir | Out-Null
New-Item -ItemType Directory -Force $smokeObjDir | Out-Null
New-Item -ItemType Directory -Force $hostDllObjDir | Out-Null
New-Item -ItemType Directory -Force $bootstrapObjDir | Out-Null
New-Item -ItemType Directory -Force $testpointsObjDir | Out-Null
New-Item -ItemType Directory -Force $OutputDir | Out-Null

if (!(Test-Path $ServerDll)) { throw "Missing server.dll: $ServerDll" }

Write-Host "[dwrt-host-smoke] cargo build -p dwrt-runtime --$Configuration"
cargo build -p dwrt-runtime --$Configuration
if ($LASTEXITCODE -ne 0) { throw "cargo build failed with exit code $LASTEXITCODE" }
if (!(Test-Path $dllPath)) { throw "Missing runtime DLL: $dllPath" }

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

Write-Host "[dwrt-host-smoke] compile DWRT host DLL"
cmd /c $compileHostDll
if ($LASTEXITCODE -ne 0) { throw "DWRT host DLL compile failed with exit code $LASTEXITCODE" }

$bootstrapSource = Join-Path $hostDir "bootstrap_smoke.cpp"
$compileBootstrap = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"{1}\\" /I"{2}" /I"{3}" "{4}" /link /OUT:"{5}"' -f $vsDevCmd, $bootstrapObjDir, $hostDir, $runtimeHeaderDir, $bootstrapSource, $bootstrapExe

Write-Host "[dwrt-host-smoke] compile DWRT host bootstrap smoke"
cmd /c $compileBootstrap
if ($LASTEXITCODE -ne 0) { throw "DWRT host bootstrap smoke compile failed with exit code $LASTEXITCODE" }

$testpointsSources = @(
    (Join-Path $hostDir "dwrt_host_testpoints.cpp"),
    (Join-Path $hostDir "testpoints_smoke.cpp")
)
$testpointsSourceArgs = ($testpointsSources | ForEach-Object { '"{0}"' -f $_ }) -join " "
$compileTestpoints = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"{1}\\" /I"{2}" {3} /link /OUT:"{4}"' -f $vsDevCmd, $testpointsObjDir, $hostDir, $testpointsSourceArgs, $testpointsExe

Write-Host "[dwrt-host-smoke] compile DWRT host testpoints smoke"
cmd /c $compileTestpoints
if ($LASTEXITCODE -ne 0) { throw "DWRT host testpoints smoke compile failed with exit code $LASTEXITCODE" }

$sources = @(
    (Join-Path $shimDir "dwrt_shadow_shim.cpp"),
    (Join-Path $hostDir "dwrt_signature_scanner.cpp"),
    (Join-Path $hostDir "dwrt_probe_manifest.cpp"),
    (Join-Path $hostDir "smoke.cpp")
)
$sourceArgs = ($sources | ForEach-Object { '"{0}"' -f $_ }) -join " "
$compile = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /std:c++20 /EHsc /W4 /WX /Fo"{1}\\" /I"{2}" /I"{3}" /I"{4}" {5} /link /OUT:"{6}"' -f $vsDevCmd, $smokeObjDir, $runtimeHeaderDir, $shimDir, $hostDir, $sourceArgs, $outExe

Write-Host "[dwrt-host-smoke] compile native DWRT host resolver smoke"
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "native DWRT host smoke compile failed with exit code $LASTEXITCODE" }

& $testpointsExe
if ($LASTEXITCODE -ne 0) { throw "DWRT host testpoints smoke failed with exit code $LASTEXITCODE" }

$bootstrapArgs = @("--host", $hostDll, "--runtime", $dllPath, "--server", $ServerDll, "--output", $bootstrapSummaryPath)
& $bootstrapExe @bootstrapArgs
if ($LASTEXITCODE -ne 0) { throw "DWRT host bootstrap smoke failed with exit code $LASTEXITCODE" }

$argumentLine = ('--server "{0}" --runtime "{1}" --require-runtime --output "{2}"' -f $ServerDll, $dllPath, $summaryPath)
if ($AllowExpectedRvaDrift) { $argumentLine += " --allow-expected-rva-drift" }
if ($MappedModuleCheck) { $argumentLine += " --mapped-module-check" }

if ($NoProfile) {
    Write-Warning "Running DWRT host smoke without profiler because -NoProfile was specified."
    $runArgs = @("--server", $ServerDll, "--runtime", $dllPath, "--require-runtime", "--output", $summaryPath)
    if ($AllowExpectedRvaDrift) { $runArgs += "--allow-expected-rva-drift" }
    if ($MappedModuleCheck) { $runArgs += "--mapped-module-check" }
    & $outExe @runArgs
    if ($LASTEXITCODE -ne 0) { throw "native DWRT host smoke failed with exit code $LASTEXITCODE" }
}
else {
    $profileParams = @{
        FilePath = $outExe
        ArgumentLine = $argumentLine
        Name = "dwrt-host-smoke"
        OutputDir = $profileDir
        Profiler = $Profiler
        XperfPreset = $XperfPreset
    }
    if ($RequireProfiler) { $profileParams.RequireProfiler = $true }
    & (Join-Path $repo "scripts\profile-dwrt-command.ps1") @profileParams
    if ($LASTEXITCODE -ne 0) { throw "profiled native DWRT host smoke failed with exit code $LASTEXITCODE" }
}

Write-Host "[dwrt-host-smoke] bootstrap summary -> $bootstrapSummaryPath"
Write-Host "[dwrt-host-smoke] resolver summary -> $summaryPath"
Write-Host "[dwrt-host-smoke] OK"
