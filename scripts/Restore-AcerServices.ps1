#Requires -RunAsAdministrator
# Re-enable Acer / Predator services this project may have disabled.

$enable = @(
    'PSSvc',
    'PredatorService',
    'AcerCCAgentSvis',
    'AcerDIAgentSvis',
    'AcerDeviceEnablingServiceV2',
    'AcerServiceSvc',
    'AcerLightingService',
    'ASMSvc',
    'AcerQAAgentSvis'
)

foreach ($name in $enable) {
    $svc = Get-Service -Name $name -ErrorAction SilentlyContinue
    if (-not $svc) {
        continue
    }
    Write-Host "enabling $name"
    Set-Service -Name $name -StartupType Manual -ErrorAction SilentlyContinue
    Start-Service -Name $name -ErrorAction SilentlyContinue
}

Get-ScheduledTask | Where-Object { $_.TaskName -match 'PredatorSense' } | ForEach-Object {
    Enable-ScheduledTask -TaskName $_.TaskName -TaskPath $_.TaskPath -ErrorAction SilentlyContinue | Out-Null
}

Write-Host "Done."
