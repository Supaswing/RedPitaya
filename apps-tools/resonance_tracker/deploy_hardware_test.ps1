param(
    [string]$Target = "root@rp-f0f8d5",
    [string]$RemoteAppDirectory = "/root/RedPitaya/apps-tools/resonance_tracker"
)

$ErrorActionPreference = "Stop"
$appDirectory = $PSScriptRoot
$files = @(
    "CMakeLists.txt",
    "hardware_test.sh",
    "src/raw_iq_acquisition.cpp",
    "src/raw_iq_acquisition.hpp",
    "tests/raw_iq_hardware_test.cpp"
)

foreach ($relativePath in $files) {
    $localPath = Join-Path $appDirectory $relativePath
    $remoteSubdirectory = Split-Path $relativePath -Parent
    $remoteDirectory = if ($remoteSubdirectory) {
        "$RemoteAppDirectory/$($remoteSubdirectory.Replace('\', '/'))"
    } else {
        $RemoteAppDirectory
    }
    & scp $localPath "${Target}:$remoteDirectory/"
    if ($LASTEXITCODE -ne 0) { throw "scp failed for $relativePath" }
}

Write-Host "Hardware-test sources copied to ${Target}:$RemoteAppDirectory"
Write-Host "Stop the web app, then run: cd $RemoteAppDirectory && sh hardware_test.sh 2>&1 | tee hardware_test.log"
