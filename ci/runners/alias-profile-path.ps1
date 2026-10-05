# Rewrites PATH entries under the real Windows profile to the neutral
# C:\ci\home junction, so tool paths printed in CI logs never show the
# profile folder name. Called from each runner's start-runner.ps1.
$ErrorActionPreference = 'Continue'
try {
    # GetFolderPath asks the shell, so it still returns the real profile
    # after a start script has pointed $env:USERPROFILE at C:\ci\home.
    $real = [Environment]::GetFolderPath('UserProfile').TrimEnd('\')
    if ($real -and $real -ne 'C:\ci\home' -and (Test-Path -LiteralPath 'C:\ci\home')) {
        $env:Path = ($env:Path -split ';' | ForEach-Object {
            if ($_ -like "$real\*" -or $_ -eq $real) { 'C:\ci\home' + $_.Substring($real.Length) } else { $_ }
        }) -join ';'
    }
} catch { }
