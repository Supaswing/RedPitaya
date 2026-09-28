$ErrorActionPreference = 'Stop'
$outputPath = Join-Path $PSScriptRoot 'output/device_journal.txt'
Write-Host 'Enter the Red Pitaya password at the SSH prompt for read-only recent service logs.'
& ssh root@rp-f0f8d5.local journalctl -n 160 --no-pager 2>&1 | Tee-Object -FilePath $outputPath
Write-Host "Journal saved to $outputPath"
Read-Host 'Press Enter to close this window'
