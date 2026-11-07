param([string]$Bundle, [string]$Ref, [string]$Event, [string]$BaseRef = '', [string]$Root = 'C:/ci/real-data')
$ErrorActionPreference = 'Continue'
$repo = "$Root/repo"
$pidFile = "$Root/run.pid"
if (Test-Path -LiteralPath $pidFile) {
    $old = Get-CimInstance Win32_Process -Filter "ProcessId=$(Get-Content -LiteralPath $pidFile)" -ErrorAction SilentlyContinue
    if ($old.CommandLine -match 'run_remote') { taskkill /T /F /PID $old.ProcessId | Out-Null }
}
if (-not (Test-Path -LiteralPath "$repo/.git")) { git init -q $repo }
git -C $repo fetch --no-tags $Bundle "+${Ref}:refs/real-data/candidate"
if ($LASTEXITCODE -ne 0) { Write-Output 'REAL_DATA_EXIT=97'; exit 97 }
Write-Output 'REAL_DATA_FETCHED'
git -C $repo clean -ffdx -e /build/
Remove-Item -LiteralPath "$repo/build/zlib-src" -Recurse -Force -ErrorAction SilentlyContinue
git -C $repo checkout -f --detach refs/real-data/candidate
if ($LASTEXITCODE -ne 0) { Write-Output 'REAL_DATA_EXIT=1'; exit 1 }
Set-Content -LiteralPath $pidFile -Value $PID -Encoding ascii
Get-Content -LiteralPath "$Root/worker.env" -Encoding UTF8 | ForEach-Object {
    if ($_ -match '^([A-Za-z_][A-Za-z0-9_]*)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
if (-not $env:FO4_DATA_DIR) { Write-Output 'FO4_DATA_DIR is not set in worker.env; the real-data tests would skip'; Write-Output 'REAL_DATA_EXIT=1'; exit 1 }
$env:GITHUB_EVENT_NAME = $Event
$env:GITHUB_BASE_REF = $BaseRef
Push-Location $repo
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'run_real_data.ps1') -Workflow '.github/workflows/native-real-data.yml'
$code = $LASTEXITCODE
Pop-Location
Write-Output "REAL_DATA_EXIT=$code"
exit $code
