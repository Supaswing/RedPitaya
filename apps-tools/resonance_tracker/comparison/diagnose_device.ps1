$ErrorActionPreference = 'Stop'
$outputPath = Join-Path $PSScriptRoot 'output/device_diagnosis.txt'
$remoteCommand = 'ps -ef'
Write-Host 'Enter the Red Pitaya password at the SSH prompt for a read-only service diagnosis.'
& ssh root@rp-f0f8d5.local $remoteCommand 2>&1 | Tee-Object -FilePath $outputPath
Write-Host "Diagnosis saved to $outputPath"
Read-Host 'Press Enter to close this window'
