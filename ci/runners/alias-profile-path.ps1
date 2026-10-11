$ErrorActionPreference = 'Continue'
try {
    # Use GetFolderPath, not $env:USERPROFILE: start-runner.ps1 has already redirected it.
    $real = [Environment]::GetFolderPath('UserProfile').TrimEnd('\')
    if ($real -and $real -ne 'C:\ci\home' -and (Test-Path -LiteralPath 'C:\ci\home')) {
        $env:Path = ($env:Path -split ';' | ForEach-Object {
            if ($_ -like "$real\*" -or $_ -eq $real) { 'C:\ci\home' + $_.Substring($real.Length) } else { $_ }
        }) -join ';'
    }
} catch { }
