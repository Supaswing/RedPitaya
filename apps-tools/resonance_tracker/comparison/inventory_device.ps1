$ErrorActionPreference = 'Stop'
$outputPath = Join-Path $PSScriptRoot 'output/device_inventory.txt'
$remoteCommand = 'git -C /root/RedPitaya status --short --branch; find /root/RedPitaya/apps-tools/resonance_tracker/test-results -maxdepth 3 -type f -printf "%p %s bytes\n" 2>/dev/null; find /root/RedPitaya/apps-tools/resonance_tracker -maxdepth 2 -type f \( -name "*.csv" -o -name "*.log" \) -printf "%p %s bytes\n" 2>/dev/null; pgrep -af "controllerhf|resonance_tracker" || true'
Write-Host 'Enter the Red Pitaya password at the SSH prompt to inventory existing capture files.'
& ssh root@rp-f0f8d5.local $remoteCommand 2>&1 | Tee-Object -FilePath $outputPath
Write-Host "Inventory saved to $outputPath"
Read-Host 'Press Enter to close this window'
