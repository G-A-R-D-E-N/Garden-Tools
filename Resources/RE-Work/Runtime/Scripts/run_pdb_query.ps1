<#
.SYNOPSIS
  Run a Ghidra GhidraScript headless against the PDB-named FO4 1.10.155 project and print its output.

.DESCRIPTION
  Wraps analyzeHeadless with -noanalysis (the DB is already fully analyzed - never re-analyze for a
  query, it would take 20-60+ minutes for nothing). Defaults to PDB_Query.java. Each remaining
  argument is passed to the script: a plain word = substring name search over function names;
  "decompile:Exact::Name" = decompile that function; "addr:0xRVA[,decompile]" = resolve an RVA.

.EXAMPLE
  .\run_pdb_query.ps1 "PlayerCharacter::Update" "TESObjectREFR"
.EXAMPLE
  .\run_pdb_query.ps1 "decompile:PlayerCharacter::Update"
.EXAMPLE
  .\run_pdb_query.ps1 -script PDB_Stats.java          # naming/health diagnostic (takes no args)
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$script = "PDB_Query.java",
    [string]$ghidra = "E:\F4SE OG\ReverseEngineering\Ghidra\ghidra_12.1_PUBLIC",
    [string]$projLoc = "E:\F4SE OG\ReverseEngineering\Projects\fwgh_proj_1.10.155",
    [string]$projName = "Fallout4_155",
    [Parameter(ValueFromRemainingArguments = $true, Position = 0)]
    [string[]]$terms
)

$env:JAVA_HOME = "C:\Program Files\Java\jdk-25"
$gh = Join-Path $ghidra "support\analyzeHeadless.bat"
$scriptDir = $PSScriptRoot
$out = Join-Path $env:TEMP ("pdbq_" + [System.IO.Path]::GetRandomFileName() + ".log")

if (-not (Test-Path $gh))      { Write-Error "analyzeHeadless not found: $gh"; return }
if (-not (Test-Path "$projLoc\$projName.gpr")) { Write-Error "project not found at $projLoc\$projName.gpr"; return }

# A killed/interrupted analyzeHeadless run leaves a project .lock file - remove it before re-running
# (same gotcha documented for the 1.10.163 pipeline in ReverseEngineering\01-SETUP.md).
Remove-Item "$projLoc\$projName.lock" -Force -ErrorAction SilentlyContinue

$cmdArgs = @($projLoc, $projName, "-process", "-recursive", "-noanalysis",
             "-scriptPath", $scriptDir, "-postScript", $script)
if ($terms) { $cmdArgs += $terms }

Write-Host "Running $script $($terms -join ' ') ..." -ForegroundColor Cyan
# Pipe blank stdin: when invoked via "cmd /c ..." (which happens under some launchers/background
# wrappers), analyzeHeadless.bat's launch.bat sets DOUBLE_CLICKED=y and calls `pause` on ANY
# non-zero exit, which hangs forever with no console attached. Feeding stdin dodges that trap.
"" | & $gh @cmdArgs *> $out

$tag = [System.IO.Path]::GetFileName($script)   # e.g. PDB_Query.java
$lines = Select-String -Path $out -Pattern "$tag>" | ForEach-Object {
    ($_.Line -replace ".*$tag> ", "") -replace " \(GhidraScript\).*", ""
}
if ($lines) { $lines } else {
    Write-Warning "No script output captured. Tail of log:"
    Get-Content $out -Tail 15
}
Remove-Item $out -Force -ErrorAction SilentlyContinue
