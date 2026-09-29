param(
    [string]$HostName = 'rp-f0f8d5.local',
    [ValidateRange(10, 300)][int]$TrackSeconds = 20,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'output'),
    [switch]$Plan
)

$ErrorActionPreference = 'Stop'
$configs = @(
    [pscustomobject]@{ Shift = 20; TrackAverages = 1; Label = 'reference' },
    [pscustomobject]@{ Shift = 19; TrackAverages = 1; Label = 'near_100Hz' },
    [pscustomobject]@{ Shift = 19; TrackAverages = 2; Label = 'same_time_as_shift20' },
    [pscustomobject]@{ Shift = 18; TrackAverages = 1; Label = 'short_window' },
    [pscustomobject]@{ Shift = 18; TrackAverages = 2; Label = 'near_100Hz' },
    [pscustomobject]@{ Shift = 18; TrackAverages = 4; Label = 'same_time_as_shift20' },
    [pscustomobject]@{ Shift = 16; TrackAverages = 1; Label = 'near_1000Hz' },
    [pscustomobject]@{ Shift = 14; TrackAverages = 1; Label = 'near_4000Hz' }
)
$matrixRows = @($configs | ForEach-Object {
    $samples = [math]::Pow(2, $_.Shift) * $_.TrackAverages
    [pscustomobject]@{
        window_shift = $_.Shift
        tracking_complex_averages = $_.TrackAverages
        samples_per_averaged_point = [long]$samples
        nominal_integration_ms = [math]::Round(1000 * $samples / 125000000, 6)
        rectangular_white_noise_enbw_hz = [math]::Round(125000000 / (2 * $samples), 3)
        label = $_.Label
    }
})
if ($Plan) { $matrixRows | Format-Table -AutoSize; return }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$runDirectory = Join-Path $OutputDirectory ('setupA_rp_matrix_' + [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $runDirectory -ErrorAction Stop | Out-Null
$matrixRows | Export-Csv -NoTypeInformation -Path (Join-Path $runDirectory 'plan.csv')
$results = [Collections.Generic.List[object]]::new()
$captureScript = Join-Path $PSScriptRoot 'run_baseline.ps1'
foreach ($row in $matrixRows) {
    $output = Join-Path $runDirectory ("shift$($row.window_shift)_avg$($row.tracking_complex_averages).jsonl")
    try {
        $summary = & $captureScript -HostName $HostName -OutputPath $output -TrackSeconds $TrackSeconds `
            -IncludeThreePoint -WindowShift $row.window_shift -CoarseAverages 3 -RefineAverages 3 `
            -TrackingAverages $row.tracking_complex_averages
        $parsed = $summary | Select-Object -Last 1 | ConvertFrom-Json
        if (-not [bool]$parsed.baseline_valid -or [int]$parsed.resonance_count -ne 2 -or
            @($parsed.tracking_runs).Count -ne 2) {
            throw "Condition shift=$($row.window_shift), averages=$($row.tracking_complex_averages) did not produce a valid two-sensor baseline and both tracking modes."
        }
        $results.Add([pscustomobject]@{
            window_shift = $row.window_shift; tracking_complex_averages = $row.tracking_complex_averages
            nominal_enbw_hz = $row.rectangular_white_noise_enbw_hz
            baseline_valid = $parsed.baseline_valid; resonance_count = $parsed.resonance_count
            status = 'complete'; capture = $output; error = ''
        })
    } catch {
        $results.Add([pscustomobject]@{
            window_shift = $row.window_shift; tracking_complex_averages = $row.tracking_complex_averages
            nominal_enbw_hz = $row.rectangular_white_noise_enbw_hz
            baseline_valid = ''; resonance_count = ''; status = 'failed'
            capture = $output; error = $_.Exception.Message
        })
        $results | Export-Csv -NoTypeInformation -Path (Join-Path $runDirectory 'results.csv')
        throw
    }
    $results | Export-Csv -NoTypeInformation -Path (Join-Path $runDirectory 'results.csv')
}
Write-Output "Saved matrix: $runDirectory"
