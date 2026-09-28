param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
function Stats($numbers) {
    $values = @($numbers | ForEach-Object { [double]$_ })
    $mean = ($values | Measure-Object -Average).Average
    $sumSq = 0.0
    foreach ($value in $values) { $sumSq += ($value - $mean) * ($value - $mean) }
    return [pscustomobject]@{
        mean = $mean
        rms = [Math]::Sqrt($sumSq / $values.Count)
        sample_sd = if ($values.Count -gt 1) { [Math]::Sqrt($sumSq / ($values.Count - 1)) } else { $null }
    }
}
$rows = @(Import-Csv -LiteralPath $InputPath)
$summary = [Collections.Generic.List[object]]::new()
foreach ($group in @($rows | Group-Object tracker_points, sensor_id)) {
    $samples = @($group.Group | Sort-Object { [double]$_.telemetry_timestamp_s })
    if ($samples.Count -lt 2) { continue }
    $frequency = Stats @($samples | ForEach-Object { $_.frequency_hz })
    $times = @($samples | ForEach-Object { [double]$_.telemetry_timestamp_s })
    $intervals = @()
    for ($i = 1; $i -lt $times.Count; ++$i) {
        $delta = $times[$i] - $times[$i - 1]
        if ($delta -le 0) { throw "Nonmonotonic telemetry time in $($group.Name)." }
        $intervals += $delta
    }
    $intervalStats = Stats $intervals
    $summary.Add([pscustomobject]@{
        platform = 'Red Pitaya'; setup_id = 'A'; sensor_state = 'static'
        tracker_points = $samples[0].tracker_points; sensor_id = $samples[0].sensor_id
        source_file = $InputPath; matched_telemetry_frames = $samples.Count
        first_sequence = $samples[0].sequence; last_sequence = $samples[-1].sequence
        mean_frequency_hz = $frequency.mean
        frequency_sd_sample_hz = $frequency.sample_sd
        df_rms_population_hz = $frequency.rms
        mean_internal_se_hz = ($samples | Measure-Object internal_se_hz -Average).Average
        mean_normalized_residual = ($samples | Measure-Object normalized_residual -Average).Average
        mean_template_gain = ($samples | Measure-Object template_gain -Average).Average
        mean_backend_rate_hz = ($samples | Measure-Object backend_rate_hz -Average).Average
        observed_telemetry_interval_s = $intervalStats.mean
        observed_telemetry_interval_sd_sample_s = $intervalStats.sample_sd
        observed_matched_telemetry_rate_hz = 1.0 / $intervalStats.mean
        all_fits_valid = @($samples | Where-Object { $_.fit_valid -ne 'True' }).Count -eq 0
        signal_delta_f_hz = ''; snr_f = ''
    })
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$summary | Export-Csv -NoTypeInformation -LiteralPath $OutputPath
$summary | ConvertTo-Json -Compress -Depth 4
