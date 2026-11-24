# Builds the mod (and the launcher when it is there); prints errors, warnings of our code and the result only.
#   powershell -ExecutionPolicy Bypass -File tools/build.ps1 [-Configure] [-Target meleenet|meleenet_launcher]
param([string]$Target = "meleenet", [switch]$Configure)

# CMake: the one on PATH, or the one that comes with the newest Visual Studio installed.
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $cmake) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -property installationPath
    if ($vs) {
      $found = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
      if (Test-Path $found) { $cmake = $found }
    }
  }
}
if (-not $cmake) { "cmake.exe is not on PATH and no Visual Studio with CMake was found"; exit 1 }

$repo = Split-Path -Parent $PSScriptRoot
if ($Configure -or -not (Test-Path "$repo\build\CMakeCache.txt")) {
  & $cmake -S $repo -B "$repo\build" -A Win32 2>&1 | Select-String -Pattern 'Error|WARNING|Steam:|Launcher' | ForEach-Object { $_.Line }
}
$out = & $cmake --build "$repo\build" --config Release --target $Target 2>&1
$bad = $out | Select-String -Pattern '(error|warning) [A-Z]+[0-9]+' | Where-Object { $_.Line -notmatch 'MSB3073|third_party' } | ForEach-Object { ($_.Line -replace '\s*\[[A-Za-z]:.*$', '') } | Select-Object -Unique -First 30
$bad
if ($LASTEXITCODE -ne 0) { "BUILD FAILED"; exit 1 }
Get-ChildItem "$repo\build\dist\Release" | ForEach-Object { "{0,10}  {1}" -f $_.Length, $_.Name }
"BUILD OK"
