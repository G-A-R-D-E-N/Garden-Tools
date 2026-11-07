$ErrorActionPreference = 'stop'
$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
if (-not (Test-Path "$installer\vswhere.exe")) { throw "Visual Studio is not installed on this runner" }
$vs = & "$installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "No Visual Studio install with the C++ x64 tools on this runner" }
$cmakeBin = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$ninjaBin = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
foreach ($tool in "$cmakeBin\cmake.exe", "$ninjaBin\ninja.exe") {
  if (-not (Test-Path $tool)) { throw "Visual Studio at $vs has no C++ CMake tools component ($tool)" }
}

# Ninja calls cl.exe directly, so later steps need the vcvars64 environment.
$env:Path = "$installer;$env:Path"
$before = @{}
Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }
$vcvars = cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed" }
$newPaths = @($cmakeBin, $ninjaBin)
foreach ($line in $vcvars) {
  if ($line -notmatch '^([^=]+)=(.*)$') { continue }
  $name = $Matches[1]; $value = $Matches[2]
  if ($name -ieq 'Path') {
    $old = $env:Path -split ';'
    $newPaths += @($value -split ';' | Where-Object { $_ -and $old -notcontains $_ })
  } elseif ($before[$name] -ne $value) {
    [IO.File]::AppendAllText($env:GITHUB_ENV, "$name=$value`n")
  }
}
# Each GITHUB_PATH line is prepended, so write in reverse to keep vcvars order.
[array]::Reverse($newPaths)
[IO.File]::AppendAllText($env:GITHUB_PATH, (($newPaths | Select-Object -Unique) -join "`n") + "`n")

$ccache = (Get-Command ccache -ErrorAction SilentlyContinue).Source
if (-not $ccache -and (Test-Path 'C:\tools\ccache\ccache.exe')) { $ccache = 'C:\tools\ccache\ccache.exe' }
if ($ccache) {
  [IO.File]::AppendAllText($env:GITHUB_ENV, "CCACHE=$ccache`n")
  [IO.File]::AppendAllText($env:GITHUB_ENV, "CCACHE_BASEDIR=$((Get-Location).Path)`n")
  & $ccache --zero-stats | Out-Null
  "Using $ccache"
} else {
  "ccache not found; building without a compiler cache"
}
