# Removes git lock files left in the shared action cache when a fetch was killed
# (e.g. the runner restarted mid-download). A leftover shallow.lock makes every
# job that uses that action fail during "workflow prepared" with
# "Unable to create ...shallow.lock: File exists".
# An action fetch takes seconds, so only locks older than 5 minutes are
# removed and a fetch another runner is doing right now is left alone.
# Called from each runner's start-runner.ps1 and every 5 minutes by the
# ForgejoRunner-clear-stale-git-locks scheduled task.
$ErrorActionPreference = 'Continue'
try {
    $cutoff = (Get-Date).AddMinutes(-5)
    foreach ($root in 'C:\ci\act', 'C:\ci\home\.cache\act') {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        # Action cache repos live at <root>\<first 2 hex>\<remaining 62 hex> of a sha256.
        Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^[0-9a-f]{2}$' } |
            Get-ChildItem -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^[0-9a-f]{62}$' } |
            Get-ChildItem -Recurse -Force -File -Filter *.lock -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -lt $cutoff } |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }
} catch { }
