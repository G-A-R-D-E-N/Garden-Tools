$ErrorActionPreference = 'Continue'
try {
    $cutoff = (Get-Date).AddMinutes(-5)
    foreach ($root in 'C:\ci\act', 'C:\ci\home\.cache\act') {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^[0-9a-f]{2}$' } |
            Get-ChildItem -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^[0-9a-f]{62}$' } |
            Get-ChildItem -Recurse -Force -File -Filter *.lock -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\.git\*' -and $_.LastWriteTime -lt $cutoff } |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }
} catch { }
