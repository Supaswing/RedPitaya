$ErrorActionPreference = 'Stop'
$base = $PSScriptRoot
$nano = 'C:\Users\bud\Orthsens\sdsi_reader\measurements\repeat_246_20260915_115728\bw100\raw.csv'
$red = Join-Path $base '..\test-results\2026-09-23\raw_iq_noise_samples.csv'
$fixture = Join-Path $base 'fixtures\two_sensor_frames.csv'
$output = Join-Path $base 'output\fixture_check'
& (Join-Path $base 'analyze.ps1') -NanoVnaRaw $nano -RedPitayaRaw $red `
    -RedPitayaTrackingCsv $fixture -OutputDir $output
$tracking = @(Import-Csv (Join-Path $output 'tracking_summary.csv'))
$nanoRow = $tracking | Where-Object platform -eq 'NanoVNA' | Select-Object -First 1
if ([int]$nanoRow.count -ne 864 -or
    [Math]::Abs([double]$nanoRow.df_rms_population_hz - 6080.040394966722) -gt 0.01 -or
    [Math]::Abs([double]$nanoRow.frame_rate_hz - 14.469052340111858) -gt 0.001) {
    throw 'NanoVNA summary disagrees with the independently published 2026-09-15 statistics.'
}
$both = $tracking | Where-Object sensor_id -eq 'both' | Select-Object -First 1
$sensor1 = $tracking | Where-Object { $_.platform -eq 'Red Pitaya' -and $_.sensor_id -eq '1' } |
    Select-Object -First 1
if ([int]$both.count -ne 3 -or [Math]::Abs([double]$both.frame_rate_hz - 10) -gt 1e-9 -or
    [Math]::Abs([double]$sensor1.frequency_sd_sample_hz - 100) -gt 1e-9) {
    throw 'Two-sensor fixture summary is wrong.'
}
Write-Host 'PASS: NanoVNA reference statistics and synthetic two-sensor frame calculations.'
