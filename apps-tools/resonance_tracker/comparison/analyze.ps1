param(
    [Parameter(Mandatory = $true)][string]$NanoVnaRaw,
    [Parameter(Mandatory = $true)][string]$RedPitayaRaw,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [string]$RedPitayaTrackingCsv = ''
)

$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::InvariantCulture
function Num([string]$value) { [double]::Parse($value, $culture) }
function Mean($values) {
    if ($values.Count -eq 0) { return $null }
    return ($values | Measure-Object -Average).Average
}
function Rms($values, [double]$center) {
    if ($values.Count -eq 0) { return $null }
    $sum = 0.0
    foreach ($value in $values) { $sum += ($value - $center) * ($value - $center) }
    return [Math]::Sqrt($sum / $values.Count)
}
function Sd($values, [double]$center) {
    if ($values.Count -lt 2) { return $null }
    return (Rms $values $center) * [Math]::Sqrt($values.Count / ($values.Count - 1))
}
function Median($values) {
    if ($values.Count -eq 0) { return $null }
    $sorted = @($values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2) { return $sorted[$middle] }
    return 0.5 * ($sorted[$middle - 1] + $sorted[$middle])
}
function Covariance($left, $right, [double]$meanLeft, [double]$meanRight) {
    if ($left.Count -lt 2 -or $left.Count -ne $right.Count) { return $null }
    $sum = 0.0
    for ($index = 0; $index -lt $left.Count; ++$index) {
        $sum += ($left[$index] - $meanLeft) * ($right[$index] - $meanRight)
    }
    return $sum / ($left.Count - 1)
}
function SummarizeComplex($samples, [string]$platform, [string]$condition, [string]$source) {
    $re = @($samples | ForEach-Object { $_.re })
    $im = @($samples | ForEach-Object { $_.im })
    $mag = @($samples | ForEach-Object { [Math]::Sqrt($_.re * $_.re + $_.im * $_.im) })
    $meanRe = Mean $re; $meanIm = Mean $im; $meanMag = Mean $mag
    $phaseCenter = [Math]::Atan2($meanIm, $meanRe)
    $phaseError = @($samples | ForEach-Object {
        $phase = [Math]::Atan2($_.im, $_.re) - $phaseCenter
        [Math]::Atan2([Math]::Sin($phase), [Math]::Cos($phase)) * 180.0 / [Math]::PI
    })
    $phaseMean = Mean $phaseError
    $sigmaRe = Sd $re $meanRe; $sigmaIm = Sd $im $meanIm
    [pscustomobject]@{
        platform = $platform; setup_id = ''; sensor_state = ''; condition = $condition
        source_file = $source; count = $samples.Count
        mean_re_gamma = $meanRe; mean_im_gamma = $meanIm
        sigma_re_gamma_sample = $sigmaRe; sigma_im_gamma_sample = $sigmaIm
        complex_noise_rms = [Math]::Sqrt([Math]::Pow((Rms $re $meanRe), 2) +
                                         [Math]::Pow((Rms $im $meanIm), 2))
        magnitude_noise_rms = Rms $mag $meanMag
        phase_noise_rms_deg = Rms $phaseError $phaseMean
        comparable_setup_verified = $false
    }
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$tracking = [Collections.Generic.List[object]]::new()
$complex = [Collections.Generic.List[object]]::new()
$rawIq = [Collections.Generic.List[object]]::new()
$nanoRows = @(Import-Csv -LiteralPath $NanoVnaRaw)
$track = [Collections.Generic.List[object]]::new()
$fixed = [Collections.Generic.List[object]]::new()
foreach ($row in $nanoRows) {
    if ($row.phase -eq 'tracking' -and $row.record -match '^RTD,([^\r\n]*)') {
        $fields = @('RTD') + @($Matches[1] -split ',')
        if ($fields.Count -ge 9 -and [int]$fields[8] -eq 5) {
            $track.Add([pscustomobject]@{ t = Num $row.elapsed_s; f = Num $fields[2]; se = Num $fields[4] })
        }
    }
    if ($row.phase -eq 'fixed' -and $row.record -match '^G,([^\r\n]*)') {
        $fields = @('G') + @($Matches[1] -split ',')
        if ($fields.Count -eq 5) {
            $fixed.Add([pscustomobject]@{ t = Num $row.elapsed_s; f = Num $fields[2]; re = Num $fields[3]; im = Num $fields[4] })
        }
    }
}
if ($track.Count -lt 2) { throw 'NanoVNA file has fewer than two valid 5-point RTD frames.' }
$trackSorted = @($track | Sort-Object t)
$frequencies = @($trackSorted | ForEach-Object { $_.f })
$se = @($trackSorted | ForEach-Object { $_.se })
$intervals = @()
for ($index = 1; $index -lt $trackSorted.Count; ++$index) {
    $delta = $trackSorted[$index].t - $trackSorted[$index - 1].t
    if ($delta -le 0) { throw 'Nonmonotonic NanoVNA RTD timestamp.' }
    $intervals += $delta
}
$meanF = Mean $frequencies; $meanInterval = Mean $intervals
$tracking.Add([pscustomobject]@{
    platform = 'NanoVNA'; setup_id = ''; sensor_state = ''; tracker_points = 5
    sensor_id = 1; source_file = $NanoVnaRaw; frame_definition = 'one sensor RTD frame'
    count = $trackSorted.Count; mean_frequency_hz = $meanF
    frequency_sd_sample_hz = Sd $frequencies $meanF
    df_rms_population_hz = Rms $frequencies $meanF
    mean_internal_se_hz = Mean $se
    mean_frame_interval_s = $meanInterval; median_frame_interval_s = Median $intervals
    frame_interval_sd_sample_s = Sd $intervals $meanInterval
    frame_rate_hz = 1.0 / $meanInterval
    signal_delta_f_hz = ''; snr_f = ''; comparable_setup_verified = $false
})
if ($fixed.Count -ge 2) {
    $frequencies = @($fixed | ForEach-Object { $_.f } | Select-Object -Unique)
    if ($frequencies.Count -ne 1) { throw 'NanoVNA fixed-frequency samples contain more than one frequency.' }
    $complex.Add((SummarizeComplex @($fixed) 'NanoVNA' "fixed_$($frequencies[0])_hz" $NanoVnaRaw))
}

$redRows = @(Import-Csv -LiteralPath $RedPitayaRaw | Where-Object { $_.mode -eq 'native' })
foreach ($group in @($redRows | Group-Object window_shift, phase_increment)) {
    $samples = [Collections.Generic.List[object]]::new()
    foreach ($row in $group.Group) {
        $incRe = Num $row.inc_i; $incIm = Num $row.inc_q
        $refRe = Num $row.ref_i; $refIm = Num $row.ref_q
        $denominator = $incRe * $incRe + $incIm * $incIm
        if ($denominator -le 0) { continue }
        $samples.Add([pscustomobject]@{
            re = ($refRe * $incRe + $refIm * $incIm) / $denominator
            im = ($refIm * $incRe - $refRe * $incIm) / $denominator
        })
    }
    if ($samples.Count -lt 2) { continue }
    $first = $group.Group[0]
    $condition = "fixed_$($first.frequency_hz)_hz_shift_$($first.window_shift)_phaseinc_$($first.phase_increment)"
    $complex.Add((SummarizeComplex @($samples) 'Red Pitaya' $condition $RedPitayaRaw))
    $incI = @($group.Group | ForEach-Object { Num $_.inc_i })
    $incQ = @($group.Group | ForEach-Object { Num $_.inc_q })
    $refI = @($group.Group | ForEach-Object { Num $_.ref_i })
    $refQ = @($group.Group | ForEach-Object { Num $_.ref_q })
    $meanIncI = Mean $incI; $meanIncQ = Mean $incQ
    $meanRefI = Mean $refI; $meanRefQ = Mean $refQ
    $rawIq.Add([pscustomobject]@{
        platform = 'Red Pitaya'; setup_id = ''; condition = $condition
        source_file = $RedPitayaRaw; count = $group.Count
        window_shift = $first.window_shift; phase_increment = $first.phase_increment
        mean_inc_i = $meanIncI; mean_inc_q = $meanIncQ
        mean_ref_i = $meanRefI; mean_ref_q = $meanRefQ
        sd_inc_i_sample = Sd $incI $meanIncI; sd_inc_q_sample = Sd $incQ $meanIncQ
        sd_ref_i_sample = Sd $refI $meanRefI; sd_ref_q_sample = Sd $refQ $meanRefQ
        covariance_inc_iq_sample = Covariance $incI $incQ $meanIncI $meanIncQ
        covariance_ref_iq_sample = Covariance $refI $refQ $meanRefI $meanRefQ
        covariance_source = 'CPU calculation from repeated raw windows'
        comparable_setup_verified = $false
    })
}
if ($RedPitayaTrackingCsv) {
    $rpFrames = @(Import-Csv -LiteralPath $RedPitayaTrackingCsv)
    foreach ($row in $rpFrames) {
        if ($row.frame_complete -ne 'true' -and $row.frame_complete -ne '1') {
            throw 'Red Pitaya capture contains an incomplete frame; retain and investigate the raw file.'
        }
    }
    foreach ($group in @($rpFrames | Group-Object tracker_points, sensor_id)) {
        $rows = @($group.Group | Sort-Object { Num $_.frame_timestamp_s })
        if ($rows.Count -lt 2) { continue }
        $times = @($rows | ForEach-Object { Num $_.frame_timestamp_s })
        $frequency = @($rows | ForEach-Object { Num $_.frequency_hz })
        $internalSe = @($rows | ForEach-Object { Num $_.internal_se_hz })
        $intervals = @()
        for ($index = 1; $index -lt $times.Count; ++$index) {
            $delta = $times[$index] - $times[$index - 1]
            if ($delta -le 0) { throw 'Nonmonotonic Red Pitaya frame timestamps.' }
            $intervals += $delta
        }
        $meanFrequency = Mean $frequency; $meanInterval = Mean $intervals
        $tracking.Add([pscustomobject]@{
            platform = 'Red Pitaya'; setup_id = ''; sensor_state = ''
            tracker_points = $rows[0].tracker_points; sensor_id = $rows[0].sensor_id
            source_file = $RedPitayaTrackingCsv; frame_definition = 'per-sensor complete frame'
            count = $rows.Count; mean_frequency_hz = $meanFrequency
            frequency_sd_sample_hz = Sd $frequency $meanFrequency
            df_rms_population_hz = Rms $frequency $meanFrequency
            mean_internal_se_hz = Mean $internalSe
            mean_frame_interval_s = $meanInterval; median_frame_interval_s = Median $intervals
            frame_interval_sd_sample_s = Sd $intervals $meanInterval
            frame_rate_hz = 1.0 / $meanInterval
            signal_delta_f_hz = ''; snr_f = ''; comparable_setup_verified = $false
        })
    }
    $completeFrames = [Collections.Generic.List[object]]::new()
    foreach ($group in @($rpFrames | Group-Object tracker_points, sequence)) {
        $ids = @($group.Group | ForEach-Object { $_.sensor_id } | Select-Object -Unique)
        if ($ids.Count -ne 2 -or $ids -notcontains '1' -or $ids -notcontains '2') {
            throw 'Red Pitaya capture has a sequence without both sensors.'
        }
        $times = @($group.Group | ForEach-Object { Num $_.frame_timestamp_s })
        if (($times | Measure-Object -Maximum).Maximum - ($times | Measure-Object -Minimum).Minimum -gt 0.001) {
            throw 'Red Pitaya sensors in one sequence have mismatched frame timestamps.'
        }
        $completeFrames.Add([pscustomobject]@{
            points = $group.Group[0].tracker_points; t = ($times | Measure-Object -Maximum).Maximum
        })
    }
    foreach ($group in @($completeFrames | Group-Object points)) {
        $times = @($group.Group | Sort-Object t | ForEach-Object { $_.t })
        if ($times.Count -lt 2) { continue }
        $intervals = @()
        for ($index = 1; $index -lt $times.Count; ++$index) {
            $delta = $times[$index] - $times[$index - 1]
            if ($delta -le 0) { throw 'Nonmonotonic Red Pitaya complete-frame timestamps.' }
            $intervals += $delta
        }
        $meanInterval = Mean $intervals
        $tracking.Add([pscustomobject]@{
            platform = 'Red Pitaya'; setup_id = ''; sensor_state = ''
            tracker_points = $group.Name; sensor_id = 'both'
            source_file = $RedPitayaTrackingCsv; frame_definition = 'complete two-sensor frame'
            count = $times.Count; mean_frequency_hz = ''; frequency_sd_sample_hz = ''
            df_rms_population_hz = ''; mean_internal_se_hz = ''
            mean_frame_interval_s = $meanInterval; median_frame_interval_s = Median $intervals
            frame_interval_sd_sample_s = Sd $intervals $meanInterval
            frame_rate_hz = 1.0 / $meanInterval
            signal_delta_f_hz = ''; snr_f = ''; comparable_setup_verified = $false
        })
    }
}
$tracking | Export-Csv -LiteralPath (Join-Path $OutputDir 'tracking_summary.csv') -NoTypeInformation
$complex | Export-Csv -LiteralPath (Join-Path $OutputDir 'complex_noise_summary.csv') -NoTypeInformation
$rawIq | Export-Csv -LiteralPath (Join-Path $OutputDir 'redpitaya_raw_iq_summary.csv') -NoTypeInformation
