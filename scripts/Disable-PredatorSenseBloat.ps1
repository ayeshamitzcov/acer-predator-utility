#Requires -RunAsAdministrator
# Stop PredatorSense from fighting Predator Utility.
# Keeps AcerService / lighting so RGB can still work.

$disable = @(
    'PSSvc',
    'PredatorService',
    'AcerCCAgentSvis',
    'AcerDIAgentSvis',
    'AcerDeviceEnablingServiceV2'
)

foreach ($name in $disable) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if (-not $svc) {
        Write-Host "skip (not installed): $name"
        continue
    }
    Write-Host "disabling $name ($($svc.DisplayName))"
    Stop-Service -Name $name -Force -ErrorAction SilentlyContinue
    Set-Service -Name $name -StartupType Disabled -ErrorAction SilentlyContinue
}

Get-ScheduledTask | Where-Object { $_.TaskName -match 'PredatorSense' } | ForEach-Object {
    Write-Host "disable task $($_.TaskPath)$($_.TaskName)"
    Disable-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath -ErrorAction SilentlyContinue | Out-Null
}

Get-Process | Where-Object { $_.ProcessName -match 'PredatorSense|PredatorSenseLauncher|PSLauncher|PSAgent|PSAdminAgent|PSSvc' } | ForEach-Object {
    Write-Host "killing $($_.ProcessName)"
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}

Write-Host "Done. Kept AcerService/lighting if they exist. See docs/disable-predatorsense.md"
