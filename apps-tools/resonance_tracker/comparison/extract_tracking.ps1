param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'
$parameterFrames = @{}
$signalFrames = @{}
$completed = [Collections.Generic.HashSet[int]]::new()
$rows = [Collections.Generic.List[object]]::new()
$pointRows = [Collections.Generic.List[object]]::new()
$rawLines = 0
$rejected = 0

function Decode-Update($record) {
    $bytes = [Convert]::FromBase64String($record.data_base64)
    if ($bytes.Length -lt 5 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'EZIA') { return $null }
    $inputStream = [IO.MemoryStream]::new($bytes, 4, $bytes.Length - 4)
    $decompressor = [IO.Compression.GzipStream]::new($inputStream, [IO.Compression.CompressionMode]::Decompress)
    $outputStream = [IO.MemoryStream]::new()
    $decompressor.CopyTo($outputStream)
    return ([Text.Encoding]::UTF8.GetString($outputStream.ToArray()) | ConvertFrom-Json)
}
function Value($object, [string]$name) {
    $property = $object.PSObject.Properties[$name]
    if ($null -eq $property) { return $null }
    return $property.Value.value
}
function Values($object, [string]$name) {
    $value = Value $object $name
    if ($null -eq $value) { return @() }
    return @($value)
}

foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $InputPath).Path)) {
    ++$rawLines
    $record = $line | ConvertFrom-Json
    $update = Decode-Update $record
    if ($null -eq $update) { continue }
    $candidates = [Collections.Generic.List[int]]::new()
    if ($update.parameters) {
        $p = $update.parameters
        $sequence = [int](Value $p 'RT_TRACK_SEQUENCE')
        if ($sequence -gt 0 -and [bool](Value $p 'RT_TRACK_COMPLETE')) {
            $parameterFrames[$sequence] = [pscustomobject]@{
                elapsed_s = [double]$record.elapsed_s; utc = $record.utc
                points = [int](Value $p 'RT_TRACK_POINTS_USED')
                sensors = [int](Value $p 'RT_TRACK_SENSOR_COUNT')
                state = [int](Value $p 'RT_STATE')
                backend_rate_hz = [double](Value $p 'RT_TRACK_RATE_HZ')
            }
            $candidates.Add($sequence)
        }
    }
    if ($update.signals) {
        $s = $update.signals
        $signalSequence = @(Values $s 'RT_TRACK_SIGNAL_SEQUENCE')
        if ($signalSequence.Count -eq 1 -and [int]$signalSequence[0] -gt 0) {
            $sequence = [int]$signalSequence[0]
            $signalFrames[$sequence] = [pscustomobject]@{
                elapsed_s = [double]$record.elapsed_s; utc = $record.utc
                ids = @(Values $s 'RT_TRACK_SENSOR_ID')
                frequency = @(Values $s 'RT_TRACK_FREQUENCY_HZ')
                se = @(Values $s 'RT_TRACK_SE_HZ')
                residual = @(Values $s 'RT_TRACK_NORMALIZED_RESIDUAL')
                gain = @(Values $s 'RT_TRACK_TEMPLATE_GAIN')
                fit_valid = @(Values $s 'RT_TRACK_FIT_VALID')
                point_ids = @(Values $s 'RT_TRACK_POINT_SENSOR_ID')
                point_offsets = @(Values $s 'RT_TRACK_POINT_OFFSET')
                point_frequency = @(Values $s 'RT_TRACK_POINT_FREQUENCY_HZ')
                point_re = @(Values $s 'RT_TRACK_POINT_RE')
                point_im = @(Values $s 'RT_TRACK_POINT_IM')
            }
            $candidates.Add($sequence)
        }
    }
    foreach ($sequence in $candidates) {
        if ($completed.Contains($sequence) -or -not $parameterFrames.ContainsKey($sequence) -or
            -not $signalFrames.ContainsKey($sequence)) { continue }
        $p = $parameterFrames[$sequence]
        $s = $signalFrames[$sequence]
        $count = $p.sensors
        $expectedPoints = $count * $p.points
        $valid = $count -eq 2 -and ($p.points -eq 3 -or $p.points -eq 5) -and
                 $s.ids.Count -eq $count -and $s.frequency.Count -eq $count -and
                 $s.se.Count -eq $count -and $s.residual.Count -eq $count -and
                 $s.gain.Count -eq $count -and $s.fit_valid.Count -eq $count -and
                 $s.point_ids.Count -eq $expectedPoints -and
                 $s.point_offsets.Count -eq $expectedPoints -and
                 $s.point_frequency.Count -eq $expectedPoints -and
                 $s.point_re.Count -eq $expectedPoints -and
                 $s.point_im.Count -eq $expectedPoints
        if ($valid) {
            foreach ($sensor in @(1, 2)) {
                $indices = @(for ($i = 0; $i -lt $count; ++$i) {
                    if ([int]$s.ids[$i] -eq $sensor) { $i }
                })
                $pointIndices = @(for ($i = 0; $i -lt $expectedPoints; ++$i) {
                    if ([int]$s.point_ids[$i] -eq $sensor) { $i }
                })
                $expectedOffsets = if ($p.points -eq 5) { @(-2, -1, 0, 1, 2) } else { @(-1, 0, 1) }
                $actualOffsets = @($pointIndices | ForEach-Object { [int]$s.point_offsets[$_] } | Sort-Object)
                if ($indices.Count -ne 1 -or $pointIndices.Count -ne $p.points -or
                    (($actualOffsets -join ',') -ne ($expectedOffsets -join ','))) { $valid = $false; break }
            }
        }
        if (-not $valid) { ++$rejected; [void]$completed.Add($sequence); continue }
        for ($i = 0; $i -lt $count; ++$i) {
            $rows.Add([pscustomobject]@{
                sequence = $sequence; sensor_id = [int]$s.ids[$i]
                tracker_points = $p.points; frequency_hz = [double]$s.frequency[$i]
                internal_se_hz = [double]$s.se[$i]
                normalized_residual = [double]$s.residual[$i]
                template_gain = [double]$s.gain[$i]
                fit_valid = [bool][int]$s.fit_valid[$i]
                telemetry_timestamp_s = [Math]::Max($p.elapsed_s, $s.elapsed_s)
                telemetry_utc = if ($p.elapsed_s -ge $s.elapsed_s) { $p.utc } else { $s.utc }
                backend_rate_hz = $p.backend_rate_hz
                frame_complete = $true
            })
        }
        for ($i = 0; $i -lt $expectedPoints; ++$i) {
            $pointRows.Add([pscustomobject]@{
                sequence = $sequence; sensor_id = [int]$s.point_ids[$i]
                tracker_points = $p.points; offset = [int]$s.point_offsets[$i]
                frequency_hz = [double]$s.point_frequency[$i]
                gamma_re = [double]$s.point_re[$i]
                gamma_im = [double]$s.point_im[$i]
            })
        }
        [void]$completed.Add($sequence)
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$rows | Sort-Object sequence, sensor_id | Export-Csv -NoTypeInformation -LiteralPath $OutputPath
$pointOutputPath = [IO.Path]::ChangeExtension($OutputPath, '.points.csv')
$pointRows | Sort-Object sequence, sensor_id, offset | Export-Csv -NoTypeInformation -LiteralPath $pointOutputPath
[pscustomobject]@{
    input = $InputPath; output = $OutputPath; point_output = $pointOutputPath
    raw_messages = $rawLines
    complete_sequences = $completed.Count - $rejected
    rejected_sequence_pairs = $rejected
    rows = $rows.Count; complex_point_rows = $pointRows.Count
    five_point_sequences = @($rows | Where-Object { $_.sensor_id -eq 1 -and $_.tracker_points -eq 5 }).Count
    three_point_sequences = @($rows | Where-Object { $_.sensor_id -eq 1 -and $_.tracker_points -eq 3 }).Count
} | ConvertTo-Json -Compress
