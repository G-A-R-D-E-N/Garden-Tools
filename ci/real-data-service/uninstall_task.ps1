$ErrorActionPreference = 'Stop'
$taskName = 'GardenRealDataService'
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    "removed $taskName"
} else {
    "$taskName is not installed"
}
