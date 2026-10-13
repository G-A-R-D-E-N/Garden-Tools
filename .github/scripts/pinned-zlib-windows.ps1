$ErrorActionPreference = 'Stop'
$cmakeText = Get-Content -Raw PluginEditTool\native\CMakeLists.txt
$revs = @([regex]::Matches($cmakeText, 'GIT_REPOSITORY[ \t]+https://github\.com/madler/zlib\.git\r?\n[ \t]*GIT_TAG[ \t]+([0-9a-f]{40})\r?\n') | ForEach-Object { $_.Groups[1].Value })
if ($revs.Count -ne 1) {
  Write-Output "::error::expected exactly one 40-hex GIT_TAG after the zlib GIT_REPOSITORY in PluginEditTool/native/CMakeLists.txt"
  exit 1
}
$zlibRev = $revs[0]
$base = if ($env:RUNNER_TOOL_CACHE) { Join-Path $env:RUNNER_TOOL_CACHE 'garden-src' } else { Join-Path $env:LOCALAPPDATA 'garden-src' }
$cache = Join-Path $base "zlib\$zlibRev"
if (Test-Path (Join-Path $cache '.installed')) {
  Write-Output "Reusing cached zlib $zlibRev"
} else {
  Write-Output "Fetching zlib $zlibRev into $cache"
  New-Item -ItemType Directory -Force (Split-Path $cache) | Out-Null
  $tmp = "$cache.tmp.$([guid]::NewGuid().ToString('N').Substring(0, 8))"
  git init -q $tmp
  if ($LASTEXITCODE -ne 0) {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $tmp
    Write-Output "::error::git init failed for $tmp"
    exit 1
  }
  for ($attempt = 1; $attempt -le 5; $attempt++) {
    git -C $tmp -c http.lowSpeedLimit=1000 -c http.lowSpeedTime=30 fetch --depth 1 https://github.com/madler/zlib.git $zlibRev
    if ($LASTEXITCODE -eq 0) { break }
    if ($attempt -eq 5) {
      Remove-Item -Recurse -Force $tmp
      Write-Output "::error::could not fetch zlib $zlibRev from GitHub after 5 attempts"
      exit 1
    }
    Write-Output "::warning::zlib fetch attempt $attempt failed, retrying"
    Start-Sleep -Seconds (15 * $attempt)
  }
  git -C $tmp checkout -q --detach FETCH_HEAD
  if ($LASTEXITCODE -ne 0) {
    Remove-Item -Recurse -Force $tmp
    Write-Output "::error::git checkout of zlib $zlibRev failed"
    exit 1
  }
  $head = git -C $tmp rev-parse HEAD
  if ($head -ne $zlibRev) {
    Remove-Item -Recurse -Force $tmp
    Write-Output "::error::zlib checkout is $head, not $zlibRev"
    exit 1
  }
  New-Item -ItemType File -Path (Join-Path $tmp '.installed') | Out-Null
  try {
    [IO.Directory]::Move($tmp, $cache)
  } catch {
    Remove-Item -Recurse -Force $tmp
    if (-not (Test-Path (Join-Path $cache '.installed'))) {
      Write-Output "::error::zlib cache $cache is incomplete"
      exit 1
    }
  }
}
New-Item -ItemType Directory -Force build | Out-Null
Copy-Item -Recurse -Force $cache build\zlib-src
Remove-Item -Recurse -Force build\zlib-src\.git
$zlibSrc = (Resolve-Path build\zlib-src).Path -replace '\\', '/'
[IO.File]::AppendAllText($env:GITHUB_ENV, "ZLIB_SRC=$zlibSrc`n")
