param(
    [string]$Target = "root@rp-f0f8d5",
    [string]$RemoteAppDirectory = "/root/RedPitaya/apps-tools/resonance_tracker",
    [string]$InstalledAppDirectory = "/opt/redpitaya/www/apps/resonance_tracker"
)

$ErrorActionPreference = "Stop"
$appDirectory = $PSScriptRoot

& scp -r `
    (Join-Path $appDirectory "index.html") `
    (Join-Path $appDirectory "css") `
    (Join-Path $appDirectory "js") `
    "${Target}:$RemoteAppDirectory/"
if ($LASTEXITCODE -ne 0) { throw "frontend source copy failed" }

$installCommand = "rw && mkdir -p '$InstalledAppDirectory/css' '$InstalledAppDirectory/js' && " +
    "cp '$RemoteAppDirectory/index.html' '$InstalledAppDirectory/index.html' && " +
    "cp -a '$RemoteAppDirectory/css/.' '$InstalledAppDirectory/css/' && " +
    "cp -a '$RemoteAppDirectory/js/.' '$InstalledAppDirectory/js/'"
& ssh $Target $installCommand
if ($LASTEXITCODE -ne 0) { throw "frontend installation failed" }

Write-Host "Frontend source and installed web files updated on $Target. Hard-refresh the browser."
