[CmdletBinding(SupportsShouldProcess)]
param([string]$Python = '')
$ErrorActionPreference = 'Stop'
$taskName = 'GardenRealDataService'
$serviceDir = 'C:\ci\real-data\service'
$source = $PSScriptRoot

function Resolve-Python([string]$given) {
    if ($given) {
        if ($given -match '\\WindowsApps\\') { throw "-Python $given is the Windows Store alias; pass the real python.exe" }
        if (-not (Test-Path -LiteralPath $given -PathType Leaf)) { throw "-Python $given does not exist" }
        return (Resolve-Path -LiteralPath $given).Path
    }
    $real = Get-Command python -All -CommandType Application -ErrorAction SilentlyContinue |
        Where-Object { $_.Source -notmatch '\\WindowsApps\\' } | Select-Object -First 1
    if (-not $real) { throw 'no python.exe on PATH outside the Windows Store alias; pass -Python <path to python.exe>' }
    $real.Source
}

$python = Resolve-Python $Python
$gh = Get-Command gh -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $gh) { throw 'gh is not on PATH; install GitHub CLI or add its folder to PATH' }
$ghPath = $gh.Source
if ($PSCmdlet.ShouldProcess($serviceDir, 'copy the service files')) {
    if ($source -ne $serviceDir) {
        New-Item -ItemType Directory -Force -Path $serviceDir | Out-Null
        Get-ChildItem -LiteralPath $source -File | Copy-Item -Destination $serviceDir -Force
    }
}
$service = Join-Path $serviceDir 'real_data_service.py'
$user = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $python -Argument "-I -B `"$service`" --gh `"$ghPath`"" -WorkingDirectory $serviceDir
$trigger = @(
    (New-ScheduledTaskTrigger -AtLogOn -User $user),
    (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 15))
)
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
    -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1)
if ($PSCmdlet.ShouldProcess($taskName, 'register the logon task')) {
    Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    Stop-ScheduledTask -TaskName $taskName
    Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" |
        Where-Object { $_.CommandLine -match 'run_real_data\.ps1' } |
        ForEach-Object { taskkill.exe /PID $_.ProcessId /T /F | Out-Null }
    Start-ScheduledTask -TaskName $taskName
    Get-ScheduledTask -TaskName $taskName | Select-Object TaskName, State | Format-List
}
"python: $python"
"gh: $ghPath"
"service: $service"
