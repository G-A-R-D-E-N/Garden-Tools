<#
.SYNOPSIS
  Run a Ghidra GhidraScript headless against the named FO4 1.10.163 project and print its output.

.DESCRIPTION
  Wraps analyzeHeadless. Defaults to FW_Query.java. Each remaining argument is passed to the script:
  a plain word = substring name search; "decompile:Exact::Name" = decompile that function.

.EXAMPLE
  .\run_query.ps1 "HUDCompass" "NavMesh::Activate"
.EXAMPLE
  .\run_query.ps1 "decompile:NavMesh::Activate"
.EXAMPLE
  .\run_query.ps1 -script FW_Stats.java          # naming/health diagnostic (takes no args)
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$script = "FW_Query.java",
    [string]$ghidra = "E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC",
    [string]$projLoc = "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.163_unpacked",
    [string]$projName = "Fallout4_163_unpacked",
    [Parameter(ValueFromRemainingArguments = $true, Position = 0)]
    [string[]]$terms
)

$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
$gh = Join-Path $ghidra "support\analyzeHeadless.bat"
$scriptDir = $PSScriptRoot
$out = Join-Path $env:TEMP ("fwq_" + [System.IO.Path]::GetRandomFileName() + ".log")

if (-not (Test-Path $gh))      { Write-Error "analyzeHeadless not found: $gh"; return }
if (-not (Test-Path "$projLoc\$projName.gpr")) { Write-Error "project not found at $projLoc\$projName.gpr"; return }

Remove-Item "$projLoc\$projName.lock" -Force -ErrorAction SilentlyContinue

$cmdArgs = @($projLoc, $projName, "-process", "-recursive", "-noanalysis",
             "-scriptPath", $scriptDir, "-postScript", $script)
if ($terms) { $cmdArgs += $terms }

Write-Host "Running $script $($terms -join ' ') ..." -ForegroundColor Cyan
# Pipe blank stdin: analyzeHeadless.bat's launch.bat sets DOUBLE_CLICKED=y under some launchers/
# background wrappers and calls `pause` on ANY non-zero exit, which hangs forever with no console
# attached -- silently holding the project lock for every later query. Feeding stdin dodges that trap
# (same fix already applied to the sibling ReverseEngineering\Scripts\run_pdb_query.ps1).
"" | & $gh @cmdArgs *> $out

$tag = [System.IO.Path]::GetFileName($script)   # e.g. FW_Query.java
$lines = Select-String -Path $out -Pattern "$tag>" | ForEach-Object {
    ($_.Line -replace ".*$tag> ", "") -replace " \(GhidraScript\).*", ""
}
if ($lines) { $lines } else {
    Write-Warning "No script output captured. Tail of log:"
    Get-Content $out -Tail 15
}
Remove-Item $out -Force -ErrorAction SilentlyContinue
