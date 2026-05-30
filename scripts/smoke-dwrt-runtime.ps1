param(
    [string]$Configuration = "release"
)

$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$targetDir = Join-Path $repo "target\$Configuration"
$dllPath = Join-Path $targetDir "dwrt_runtime.dll"
$libPath = Join-Path $targetDir "dwrt_runtime.dll.lib"
$headerDir = Join-Path $repo "crates\dwrt-runtime\include"
$smokeSource = Join-Path $repo "tests\smoke\dwrt_runtime_abi_smoke.c"
$outDir = Join-Path $repo "target\smoke"
$outExe = Join-Path $outDir "dwrt_runtime_abi_smoke.exe"

New-Item -ItemType Directory -Force $outDir | Out-Null

Write-Host "[dwrt-smoke] cargo build -p dwrt-runtime --$Configuration"
cargo build -p dwrt-runtime --$Configuration

if (!(Test-Path $dllPath)) { throw "Missing runtime DLL: $dllPath" }
if (!(Test-Path $libPath)) { throw "Missing runtime import lib: $libPath" }

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
if (!(Test-Path $vsDevCmd)) {
    $vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
}
if (!(Test-Path $vsDevCmd)) { throw "Could not find VsDevCmd.bat" }

$compile = '"{0}" -arch=x64 -host_arch=x64 >NUL && cl /nologo /W4 /WX /I"{1}" "{2}" /link /LIBPATH:"{3}" dwrt_runtime.dll.lib /OUT:"{4}"' -f $vsDevCmd, $headerDir, $smokeSource, $targetDir, $outExe
Write-Host "[dwrt-smoke] compile C ABI smoke test"
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "C smoke compile failed with exit code $LASTEXITCODE" }

Write-Host "[dwrt-smoke] run C ABI smoke test"
$oldPath = $env:PATH
try {
    $env:PATH = "$targetDir;$oldPath"
    & $outExe
    if ($LASTEXITCODE -ne 0) { throw "C smoke test failed with exit code $LASTEXITCODE" }
}
finally {
    $env:PATH = $oldPath
}

Write-Host "[dwrt-smoke] exports"
$dump = '"{0}" -arch=x64 -host_arch=x64 >NUL && dumpbin /exports "{1}" | findstr dwrt_' -f $vsDevCmd, $dllPath
cmd /c $dump
if ($LASTEXITCODE -ne 0) { throw "Export smoke check failed with exit code $LASTEXITCODE" }

Write-Host "[dwrt-smoke] OK"
