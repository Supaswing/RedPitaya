$ErrorActionPreference = 'Stop'
$outputPath = Join-Path $PSScriptRoot 'output/device_probe.txt'
New-Item -ItemType Directory -Force -Path (Split-Path $outputPath) | Out-Null
$remoteCommand = 'date -u; uname -a; test -d /root/RedPitaya && git -C /root/RedPitaya rev-parse HEAD; test -d /root/RedPitaya/apps-tools/resonance_tracker && ls /root/RedPitaya/apps-tools/resonance_tracker; test -d /opt/redpitaya/www/apps/resonance_tracker && ls /opt/redpitaya/www/apps/resonance_tracker'
Write-Host 'Connect to root@rp-f0f8d5.local and enter the device password at the SSH prompt.'
& ssh root@rp-f0f8d5.local $remoteCommand 2>&1 | Tee-Object -FilePath $outputPath
Write-Host "Device probe saved to $outputPath"
Read-Host 'Press Enter to close this window'
