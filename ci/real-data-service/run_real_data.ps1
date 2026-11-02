param([string]$Workflow = '.github\workflows\native-real-data.yml', [switch]$ListOnly)
$ErrorActionPreference = 'Stop'

function Get-Scalar([string]$value) {
    if ($value -match "^'(.*)'$") { return $Matches[1].Replace("''", "'") }
    if ($value -match '^"(.*)"$') { return $Matches[1] }
    return $value
}

function Read-Workflow([string]$path) {
    $vars = [ordered]@{}
    $steps = New-Object System.Collections.ArrayList
    $step = $null
    $block = $false
    $inEnv = $false
    foreach ($line in [IO.File]::ReadAllText($path) -split "`r?`n") {
        if ($block) {
            if ($line -eq '' -or $line.StartsWith('          ')) {
                $step.Run += $line.Substring([Math]::Min(10, $line.Length)) + "`n"
                continue
            }
            $block = $false
        }
        if ($line -match '^\S') { $inEnv = ($line -eq 'env:'); $step = $null }
        if ($inEnv -and $line -match '^  (\w+): (.*)$') { $vars[$Matches[1]] = Get-Scalar $Matches[2]; continue }
        if ($line -match '^      - name: (.*)$') {
            $step = [pscustomobject]@{ Name = $Matches[1]; Shell = 'cmd'; Run = $null; Always = $false }
            [void]$steps.Add($step)
        } elseif ($step -and $line -match '^        shell: (\w+)$') {
            $step.Shell = $Matches[1]
        } elseif ($step -and $line -match '^        if: (.*)$') {
            $step.Always = $Matches[1] -match 'always\(\)'
        } elseif ($step -and $line -match '^        run: \|$') {
            $step.Run = ''
            $block = $true
        } elseif ($step -and $line -match '^        run: (.+)$') {
            $step.Run = $Matches[1]
        }
    }
    [pscustomobject]@{ Env = $vars; Steps = $steps }
}

$workflowPath = (Resolve-Path -LiteralPath $Workflow).Path
$wf = Read-Workflow $workflowPath
if ($ListOnly) {
    $wf.Env.Keys | ForEach-Object { "env $_" }
    $wf.Steps | ForEach-Object { "step [$($_.Shell)] run=$($null -ne $_.Run) always=$($_.Always): $($_.Name)" }
    exit 0
}
if (-not $wf.Env.Contains('REAL_DATA_TEST_REGEX')) { throw "REAL_DATA_TEST_REGEX not found in $Workflow" }
foreach ($key in $wf.Env.Keys) { Set-Item -Path "env:$key" -Value $wf.Env[$key] }

$work = Join-Path ([IO.Path]::GetTempPath()) ('real-data-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $work | Out-Null
$status = 0
try {
    foreach ($name in 'GITHUB_ENV', 'GITHUB_PATH', 'GITHUB_OUTPUT') {
        $file = Join-Path $work $name
        New-Item -ItemType File -Path $file | Out-Null
        Set-Item -Path "env:$name" -Value $file
    }
    $pathSeen = 0
    $index = 0
    foreach ($step in $wf.Steps) {
        $index++
        if ($null -eq $step.Run) { continue }
        if ($status -ne 0 -and -not $step.Always) { continue }
        Write-Output "==> $($step.Name)"
        if ($step.Shell -eq 'powershell') {
            $file = Join-Path $work ('step{0:D2}.ps1' -f $index)
            $body = "`$ErrorActionPreference = 'stop'`r`n" + $step.Run.Replace("`n", "`r`n") + "`r`nif (Test-Path -LiteralPath variable:LASTEXITCODE) { exit `$LASTEXITCODE }`r`n"
            [IO.File]::WriteAllText($file, $body, (New-Object Text.UTF8Encoding $false))
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $file
        } else {
            $file = Join-Path $work ('step{0:D2}.cmd' -f $index)
            [IO.File]::WriteAllText($file, $step.Run.Replace("`n", "`r`n"), [Text.Encoding]::ASCII)
            & cmd.exe /d /c $file
        }
        $code = $LASTEXITCODE
        foreach ($line in [IO.File]::ReadAllLines($env:GITHUB_ENV)) {
            if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
        }
        $paths = [IO.File]::ReadAllLines($env:GITHUB_PATH)
        for ($i = $pathSeen; $i -lt $paths.Count; $i++) {
            if ($paths[$i]) { $env:Path = "$($paths[$i]);$env:Path" }
        }
        $pathSeen = $paths.Count
        if ($code -ne 0 -and $status -eq 0) { $status = $code }
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
exit $status
