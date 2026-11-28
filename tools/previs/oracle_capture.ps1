param(
  [Parameter(Mandatory = $true)][string]$DataDir,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [string]$OracleExtract = "",
  [int]$Limit = 5
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$exe = $OracleExtract
if ([string]::IsNullOrWhiteSpace($exe)) {
  $guess = Join-Path $PSScriptRoot "..\..\build\previs602\bin\previs_oracle_extract.exe"
  if (Test-Path $guess) { $exe = $guess }
}

if (Test-Path $exe) {
  & $exe $DataDir $OutDir --limit $Limit
  if ($LASTEXITCODE -eq 0) { exit 0 }
  Write-Warning "previs_oracle_extract exited $LASTEXITCODE; falling back to loose-file scan."
}

$manifest = @{ build = (Get-Item "$DataDir\..\Fallout4.exe").VersionInfo.FileVersion; files = @() }
Get-ChildItem -Path $DataDir -Recurse -Include "*_OC.NIF" -ErrorAction SilentlyContinue |
  Select-Object -First $Limit |
  ForEach-Object {
    $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant()
    $rel = $_.FullName.Substring($DataDir.Length).TrimStart('\')
    $dest = Join-Path $OutDir $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    Copy-Item $_.FullName $dest -Force
    $manifest.files += @{ path = $rel.Replace('\', '/'); sha256 = $hash; bytes = $_.Length }
  }
$manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutDir "manifest.json") -Encoding UTF8
