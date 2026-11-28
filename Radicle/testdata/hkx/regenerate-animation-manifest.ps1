# Regenerates a committed HKX animation manifest with BGS's own reader.
# The generator is BGS/tools/tests/HkxAnimationManifestTests.cs:WritesTheRequestedClipManifest; this script only
# supplies the clip and output paths, so the committed manifest comes from BGS and not from a Radicle reimplementation.
param(
    [Parameter(Mandatory = $true)][string]$Clip,
    [Parameter(Mandatory = $true)][string]$Out
)

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$env:HKX_MANIFEST_HKX = (Resolve-Path $Clip).Path
$env:HKX_MANIFEST_OUT = [System.IO.Path]::GetFullPath($Out)

if (Test-Path $env:HKX_MANIFEST_OUT) { Remove-Item $env:HKX_MANIFEST_OUT }
dotnet test (Join-Path $repo 'BGS/tools/tests/BehaviourGraph.Tests.csproj') `
    --filter 'FullyQualifiedName~WritesTheRequestedClipManifest'
if ($LASTEXITCODE -ne 0) {
    throw "BGS manifest generation failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path $env:HKX_MANIFEST_OUT)) {
    throw "BGS did not write $env:HKX_MANIFEST_OUT"
}
Write-Host "wrote $env:HKX_MANIFEST_OUT"
