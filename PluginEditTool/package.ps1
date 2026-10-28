[CmdletBinding()]
param(
    [string] $Version = (Get-Date -Format 'yyyy.MM.dd'),
    [string] $NiftoolPath = $env:NIFTOOL_PATH,
    [string] $TexconvPath = $env:TEXCONV_PATH,
    [string] $CkWikiPath = $env:CK_WIKI_PATH,
    [string] $AudioToolsDir = $env:FO4IDE_AUDIO_TOOLS_DIR,
    [string] $BaseScriptsPath = $env:FO4IDE_BASE_SCRIPTS_DIR,
    [switch] $SkipWeb,
    [switch] $SkipCkWiki,
    [switch] $SkipAudioTools,
    [switch] $IncludeBaseScripts
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root        = $PSScriptRoot
$NativeDir   = Join-Path $Root 'native'
$NativeBuild = Join-Path $NativeDir 'build\package-win-x64'
$WebDir      = Join-Path $Root 'web'
$Staging     = Join-Path $Root "dist\FO4RecordEditor-$Version"
$ZipPath     = Join-Path $Root "dist\FO4RecordEditor-$Version.zip"

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Warn($msg) { Write-Host "    ! $msg" -ForegroundColor Yellow }

Step 'Stopping any running FO4RecordEditor.exe'
Get-Process FO4RecordEditor -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

if (-not $SkipWeb) {
    Step 'Building the web UI'
    Push-Location $WebDir
    try {
        if (Test-Path 'package-lock.json') { npm ci } else { npm install }
        if ($LASTEXITCODE -ne 0) { throw 'npm install failed.' }
        npm run build
        if ($LASTEXITCODE -ne 0) { throw 'npm run build failed.' }
    } finally { Pop-Location }
} else {
    Warn 'Skipping the web build (-SkipWeb). web\dist\ must already be current.'
}

$WebDist = Join-Path $WebDir 'dist'
if (-not (Test-Path $WebDist)) {
    throw "web\dist\ does not exist. Run without -SkipWeb; the native window will not render without it."
}

Step 'Configuring the native C++ host'
$Generator = if ($env:FO4IDE_CMAKE_GENERATOR) { $env:FO4IDE_CMAKE_GENERATOR } else { 'Visual Studio 17 2022' }
$Configure = @(
    '-S', $NativeDir,
    '-B', $NativeBuild,
    '-G', $Generator,
    '-DFO4IDE_BUILD_TESTS=OFF',
    '-DFO4IDE_BUILD_HTTP=ON',
    # Force the pinned static fallback so release packages never depend on an
    # arbitrary developer-machine zlib DLL.
    '-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON'
)
if ($Generator -match 'Visual Studio') {
    $Configure += @('-A', 'x64')
} else {
    # A single-config generator requires the build type at configure time;
    # --config Release is otherwise a silent no-op and ships a Debug exe.
    $Configure += @('-DCMAKE_BUILD_TYPE=Release')
}
if ($env:ZLIB_SRC) {
    # The CI runner must reuse its cached pinned zlib instead of cloning GitHub.
    $Configure += "-DFETCHCONTENT_SOURCE_DIR_FO4IDE_ZLIB=$env:ZLIB_SRC"
}
if ($env:CCACHE) {
    # The release lane sets up ccache, so it must actually reach the compilers.
    $Configure += @("-DCMAKE_C_COMPILER_LAUNCHER=$env:CCACHE", "-DCMAKE_CXX_COMPILER_LAUNCHER=$env:CCACHE")
}
$Configured = $false
for ($attempt = 1; $attempt -le 3; $attempt++) {
    & cmake @Configure
    if ($LASTEXITCODE -eq 0) { $Configured = $true; break }
    if ($attempt -lt 3) {
        Write-Warning "CMake configure failed (attempt $attempt); retrying because CI egress is flaky."
        Start-Sleep -Seconds (15 * $attempt)
    }
}
if (-not $Configured) { throw 'Native CMake configure failed.' }

Step 'Building FO4RecordEditor.exe (Release)'
cmake --build $NativeBuild --config Release --target fo4ide
if ($LASTEXITCODE -ne 0) { throw 'Native C++ build failed. Scroll up for the compiler error.' }

$NativeExe = Get-ChildItem $NativeBuild -Filter 'FO4RecordEditor.exe' -Recurse -File |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($null -eq $NativeExe) {
    throw "Native build completed but FO4RecordEditor.exe was not found under $NativeBuild"
}

Step 'Staging the package'
if (Test-Path $Staging) { Remove-Item $Staging -Recurse -Force }
New-Item -ItemType Directory -Path $Staging -Force | Out-Null
Copy-Item $NativeExe.FullName (Join-Path $Staging 'FO4RecordEditor.exe') -Force

$WebStage = Join-Path $Staging 'web\dist'
New-Item -ItemType Directory -Path $WebStage -Force | Out-Null
Copy-Item (Join-Path $WebDist '*') $WebStage -Recurse -Force

Step 'Bundling optional external tools'
if ($NiftoolPath -and (Test-Path -LiteralPath $NiftoolPath -PathType Leaf)) {
    $dest = Join-Path $Staging 'tools\niftool'
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Copy-Item -LiteralPath $NiftoolPath (Join-Path $dest 'niftool.exe') -Force
    Write-Host "    niftool ($NiftoolPath)"
} else {
    Warn "niftool not bundled: -NiftoolPath / NIFTOOL_PATH is unset or not a file ('$NiftoolPath'); the nif_* external-tool operations will be unavailable."
}

if ($TexconvPath -and (Test-Path -LiteralPath $TexconvPath -PathType Leaf)) {
    $dest = Join-Path $Staging 'tools\texconv'
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    Copy-Item -LiteralPath $TexconvPath (Join-Path $dest 'Texconvx64.exe') -Force
    Write-Host "    texconv ($TexconvPath)"
} else {
    Warn "texconv not bundled: -TexconvPath / TEXCONV_PATH is unset or not a file ('$TexconvPath'); native DDS support still works, but texconv-only fallbacks will be unavailable."
}

if (-not $SkipCkWiki) {
    if ($CkWikiPath -and (Test-Path -LiteralPath $CkWikiPath -PathType Container)) {
        $dest = Join-Path $Staging 'tools\ckwiki\fallout4'
        New-Item -ItemType Directory -Path $dest -Force | Out-Null
        Copy-Item -Path (Join-Path ([WildcardPattern]::Escape($CkWikiPath)) '*') $dest -Recurse -Force
        $count = (Get-ChildItem $dest -Filter '*.html' -Recurse -File).Count
        Write-Host "    CK Wiki mirror ($count pages)"
    } else {
        Warn "CK wiki mirror not bundled: -CkWikiPath / CK_WIKI_PATH is unset or not a folder ('$CkWikiPath'); offline wiki lookups then need CK_WIKI_PATH on the target machine."
    }
} else {
    Warn 'Skipping the CK Wiki mirror (-SkipCkWiki).'
}

if (-not $SkipAudioTools) {
    if ($AudioToolsDir -and (Test-Path -LiteralPath $AudioToolsDir -PathType Container)) {
        $dest = Join-Path $Staging 'tools\audio'
        New-Item -ItemType Directory -Path $dest -Force | Out-Null
        $missing = @()
        foreach ($exe in 'ffmpeg.exe', 'xWMAEncode.exe', 'BmlFuzEncode.exe', 'BmlFuzDecode.exe') {
            $src = Join-Path $AudioToolsDir $exe
            if (Test-Path $src) { Copy-Item $src $dest -Force } else { $missing += $exe }
        }
        if ($missing.Count -eq 0) {
            Write-Host '    ffmpeg.exe, xWMAEncode.exe, BmlFuzEncode.exe, BmlFuzDecode.exe'
        } else {
            Warn "Missing from ${AudioToolsDir}: $($missing -join ', ')"
        }
    } else {
        Warn "Audio tools not bundled: -AudioToolsDir / FO4IDE_AUDIO_TOOLS_DIR is unset or not a folder ('$AudioToolsDir'); configure FFMPEG_PATH/XWMAENCODE_PATH on the target machine if needed."
    }
} else {
    Warn 'Skipping the audio tools (-SkipAudioTools).'
}

if ($IncludeBaseScripts) {
    if ($BaseScriptsPath -and (Test-Path -LiteralPath $BaseScriptsPath -PathType Container)) {
        $dest = Join-Path $Staging 'tools\papyrus\Base'
        New-Item -ItemType Directory -Path $dest -Force | Out-Null
        Copy-Item -Path (Join-Path ([WildcardPattern]::Escape($BaseScriptsPath)) '*') $dest -Recurse -Force
        Warn 'Bundled vanilla Papyrus base scripts. These are Bethesda assets; do not redistribute this build.'
    } else {
        Warn "-IncludeBaseScripts was set but -BaseScriptsPath / FO4IDE_BASE_SCRIPTS_DIR is unset or not a folder ('$BaseScriptsPath')."
    }
}

Step 'Adding docs and the sample MCP config'
foreach ($f in 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md') {
    Copy-Item (Join-Path $Root $f) $Staging -Force
}

$ShippedDocs = 'MCP_SETUP.md'
$DocsDest = New-Item -ItemType Directory -Path (Join-Path $Staging 'docs') -Force
foreach ($d in $ShippedDocs) {
    $src = Join-Path $Root "docs\$d"
    if (-not (Test-Path $src)) { throw "Shipped doc missing: $src" }
    Copy-Item $src $DocsDest -Force
}

@'
{
  "mcpServers": {
    "fo4editor": {
      "type": "stdio",
      "command": "C:\\PUT\\THE\\FULL\\PATH\\HERE\\FO4RecordEditor.exe",
      "args": ["--mcp", "--mo2", "C:\\PUT\\YOUR\\MO2\\INSTANCE\\HERE"]
    }
  }
}
'@ | Set-Content (Join-Path $Staging 'mcp.sample.json') -Encoding UTF8

Step 'Writing build metadata'
$Commit = 'unknown'
try {
    $resolved = (& git -C $Root rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -eq 0 -and $resolved) { $Commit = $resolved.Trim() }
} catch {
    $Commit = 'unknown'
}
$Abi = 'unknown'
$AbiHeader = Join-Path $Root '..\Radicle\include\fo4\c_api.h'
if (Test-Path -LiteralPath $AbiHeader) {
    $define = Select-String -LiteralPath $AbiHeader -Pattern '^\s*#define\s+RADICLE_ABI_VERSION\s+(\d+)u?\s*$' |
        Select-Object -First 1
    if ($define) { $Abi = $define.Matches[0].Groups[1].Value }
}
$BuildInfo = @(
    'product=FO4RecordEditor'
    "version=$Version"
    "commit=$Commit"
    "radicle_abi_version=$Abi"
    "built_at_utc=$((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))"
    'runtime=native C++23; no .NET runtime required'
    'requires=Microsoft Edge WebView2 Runtime (x64); Microsoft Visual C++ 2015-2022 Redistributable (x64)'
)
Set-Content -LiteralPath (Join-Path $Staging 'build-info.txt') -Value $BuildInfo -Encoding ascii
Write-Host "    version=$Version commit=$Commit"

Step 'Zipping'
if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
Compress-Archive -Path "$Staging\*" -DestinationPath $ZipPath -CompressionLevel Optimal

$sizeMb = [math]::Round((Get-Item $ZipPath).Length / 1MB, 1)
Write-Host "`n============================================================" -ForegroundColor Green
Write-Host " Packaged: $ZipPath" -ForegroundColor Green
Write-Host " Size:     $sizeMb MB"
Write-Host ' Runtime:  native C++23; no .NET runtime required'
Write-Host ' Needs:    Microsoft Edge WebView2 Runtime (x64)'
Write-Host '           Microsoft Visual C++ 2015-2022 Redistributable (x64)'
Write-Host "============================================================`n" -ForegroundColor Green