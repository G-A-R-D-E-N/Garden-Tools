param(
  [string]$CacheDir
)

$ErrorActionPreference = 'Stop'

$version = '4.14.2'
$sha256 = '6993ce645e53192cec592ee23186200b574ac4c59097ecde8d9cfd681b47ae84'
$name = "ccache-$version-windows-x86_64"
$root = Join-Path $env:RUNNER_TEMP $name
$exe = Join-Path $root 'ccache.exe'

if (-not (Test-Path $exe)) {
  $archive = Join-Path $env:RUNNER_TEMP "ccache-$version.zip"
  Write-Output "Fetching ccache $version"
  foreach ($attempt in 1..3) {
    try {
      Invoke-WebRequest "https://github.com/ccache/ccache/releases/download/v$version/$name.zip" -OutFile $archive
      break
    } catch {
      if ($attempt -eq 3) { throw }
      Write-Output "ccache download failed on attempt $attempt, retrying: $($_.Exception.Message)"
      Start-Sleep -Seconds (15 * $attempt)
    }
  }
  $actual = (Get-FileHash $archive -Algorithm SHA256).Hash
  if ($actual -ne $sha256) { throw "ccache checksum mismatch: expected $sha256, got $actual" }
  Expand-Archive $archive -DestinationPath $env:RUNNER_TEMP -Force
  if (-not (Test-Path $exe)) { throw "ccache archive did not contain ccache.exe under $root" }
}

[IO.File]::AppendAllText($env:GITHUB_PATH, $root + "`n")
[IO.File]::AppendAllText($env:GITHUB_ENV, "CCACHE=$exe`n")
[IO.File]::AppendAllText($env:GITHUB_ENV, "CCACHE_BASEDIR=$((Get-Location).Path)`n")
if ($CacheDir) {
  [IO.File]::AppendAllText($env:GITHUB_ENV, "CCACHE_DIR=$CacheDir`n")
}
Write-Output "ccache $version at $exe"
