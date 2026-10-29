[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $StagingDir,
    [Parameter(Mandatory)] [string[]] $Id,
    [string] $ManifestPath = (Join-Path $PSScriptRoot 'bundled-tools.json')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Windows PowerShell 5.1 negotiates TLS 1.0 by default; GitHub requires TLS 1.2.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Save-Url([string] $Url, [string] $Path) {
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
            return
        } catch {
            if ($attempt -eq 3) { throw "download failed after 3 attempts: $Url ($($_.Exception.Message))" }
            Write-Warning "download attempt $attempt failed, retrying: $Url"
            Start-Sleep -Seconds (10 * $attempt)
        }
    }
}

$manifest = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json

foreach ($wanted in $Id) {
    $found = @($manifest.tools | Where-Object { $_.id -eq $wanted })
    if ($found.Count -ne 1) { throw "bundled-tools.json must list exactly one tool with id '$wanted'." }
    $tool = $found[0]
    $install = Join-Path $StagingDir ($tool.file -replace '/', '\')
    New-Item -ItemType Directory -Force -Path (Split-Path $install) | Out-Null
    $download = Join-Path ([IO.Path]::GetTempPath()) "fo4ide-$([guid]::NewGuid().ToString('N')).download"
    try {
        Save-Url $tool.url $download
        $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $download).Hash.ToLowerInvariant()
        if ($actual -ne $tool.sha256) {
            throw "$($tool.name): SHA256 is $actual, expected $($tool.sha256). Refusing to package it."
        }
        if ($tool.member) {
            $zip = [IO.Compression.ZipFile]::OpenRead($download)
            try {
                $entries = @($zip.Entries | Where-Object {
                    $name = $_.FullName -replace '\\', '/'
                    $name -eq $tool.member -or $name.EndsWith('/' + $tool.member)
                })
                if ($entries.Count -ne 1) {
                    throw "$($tool.name): expected one '$($tool.member)' in the archive, found $($entries.Count)."
                }
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $install, $true)
            } finally { $zip.Dispose() }
        } else {
            Copy-Item -LiteralPath $download -Destination $install -Force
        }
    } finally {
        Remove-Item -LiteralPath $download -Force -ErrorAction SilentlyContinue
    }
    Save-Url $tool.licenseUrl (Join-Path (Split-Path $install) "LICENSE-$($tool.id).txt")
    Write-Host "    $($tool.name) $($tool.version) (sha256 $actual)"
}
