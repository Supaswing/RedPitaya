$ErrorActionPreference = 'Stop'
$outputPath = Join-Path $PSScriptRoot 'output/device_error_log_paths.txt'
Write-Host 'Enter the Red Pitaya password at the SSH prompt to list server error logs.'
& ssh root@rp-f0f8d5.local find /var/log /opt/redpitaya/www -maxdepth 3 -iname '*error*' 2>&1 |
    Tee-Object -FilePath $outputPath
Write-Host "Paths saved to $outputPath"
Read-Host 'Press Enter to close this window'
