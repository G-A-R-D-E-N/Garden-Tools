param(
  [Parameter(Mandatory = $true)][string]$DataDir,
  [Parameter(Mandatory = $true)][string]$OutDir
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$manifest = @{ build = (Get-Item "$DataDir\..\Fallout4.exe").VersionInfo.FileVersion; files = @() }
Get-ChildItem -Path $DataDir -Recurse -Include "*_OC.NIF" -ErrorAction SilentlyContinue |
  Select-Object -First 5 |
  ForEach-Object {
    $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash.ToLowerInvariant()
    $rel = $_.FullName.Substring($DataDir.Length).TrimStart('\')
    $dest = Join-Path $OutDir $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    Copy-Item $_.FullName $dest -Force
    $manifest.files += @{ path = $rel.Replace('\', '/'); sha256 = $hash; bytes = $_.Length }
  }
$manifest | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutDir "manifest.json") -Encoding UTF8
