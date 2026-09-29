param(
    [string]$HostName = 'rp-f0f8d5.local',
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [ValidateRange(0, 300)][int]$TrackSeconds = 0,
    [switch]$IncludeThreePoint,
    [ValidateRange(0, 20)][int]$WindowShift = 17,
    [ValidateRange(1, 32)][int]$CoarseAverages = 3,
    [ValidateRange(1, 32)][int]$RefineAverages = 3,
    [ValidateRange(1, 8)][int]$TrackingAverages = 1
)

$ErrorActionPreference = 'Stop'
function Send-Parameters($socket, $parameters) {
    $bytes = [Text.Encoding]::UTF8.GetBytes((@{ parameters = $parameters } | ConvertTo-Json -Compress -Depth 5))
    $socket.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text,
                      $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null
}
function Receive-Update($socket, $buffer, $clock, $writer) {
    $message = [IO.MemoryStream]::new()
    do {
        $deadline = [Threading.CancellationTokenSource]::new(10000)
        try {
            $part = $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $deadline.Token).GetAwaiter().GetResult()
        } finally { $deadline.Dispose() }
        if ($part.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { throw 'WebSocket closed.' }
        $message.Write($buffer, 0, $part.Count)
    } while (-not $part.EndOfMessage)
    $bytes = $message.ToArray()
    $writer.WriteLine(([pscustomobject]@{
        utc = [DateTime]::UtcNow.ToString('o'); elapsed_s = $clock.Elapsed.TotalSeconds
        type = $part.MessageType.ToString(); data_base64 = [Convert]::ToBase64String($bytes)
    } | ConvertTo-Json -Compress))
    $prefix = [Text.Encoding]::ASCII.GetString($bytes, 0, 4)
    if ($prefix -ne 'EZIA') { return $null }
    $inputStream = [IO.MemoryStream]::new($bytes, 4, $bytes.Length - 4)
    $decompressor = [IO.Compression.GzipStream]::new($inputStream, [IO.Compression.CompressionMode]::Decompress)
    $outputStream = [IO.MemoryStream]::new()
    $decompressor.CopyTo($outputStream)
    return ([Text.Encoding]::UTF8.GetString($outputStream.ToArray()) | ConvertFrom-Json)
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$socket = [Net.WebSockets.ClientWebSocket]::new()
$socket.ConnectAsync([Uri]::new("ws://$HostName/wss"), [Threading.CancellationToken]::None).GetAwaiter().GetResult()
$writer = [IO.StreamWriter]::new([IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew), [Text.Encoding]::UTF8)
$clock = [Diagnostics.Stopwatch]::StartNew()
$buffer = New-Object byte[] 65536
$latest = @{}
$signals = @{}
try {
    Send-Parameters $socket @{ in_command = @{ value = 'send_all_params' } }
    while (-not $latest.ContainsKey('RT_COMMAND_ACK') -and $clock.Elapsed.TotalSeconds -lt 10) {
        $update = Receive-Update $socket $buffer $clock $writer
        if ($update -and $update.parameters) {
            foreach ($property in $update.parameters.PSObject.Properties) { $latest[$property.Name] = $property.Value.value }
        }
    }
    if (-not $latest.ContainsKey('RT_COMMAND_ACK')) { throw 'No parameter snapshot received.' }
    if ([int]$latest.RT_STATE -ne 0 -and [int]$latest.RT_STATE -ne 3) {
        throw "App is not idle or baseline-ready (RT_STATE=$($latest.RT_STATE)); no settings changed."
    }
    $sequence = [int]$latest.RT_COMMAND_ACK + 1
    $settings = @{
        RT_WINDOW_SHIFT = @{ value = $WindowShift }
        RT_BASELINE_START_HZ = @{ value = 20000000 }
        RT_BASELINE_STOP_HZ = @{ value = 26000000 }
        RT_BASELINE_SENSOR_COUNT = @{ value = 2 }
        RT_SENSOR_ENABLE_MASK = @{ value = 3 }
        RT_BASELINE_OVERVIEW_POINTS = @{ value = 151 }
        RT_BASELINE_FILTER_RADIUS = @{ value = 2 }
        RT_BASELINE_COARSE_AVERAGES = @{ value = $CoarseAverages }
        RT_BASELINE_REFINE_POINTS = @{ value = 21 }
        RT_BASELINE_REFINE_AVERAGES = @{ value = $RefineAverages }
        RT_TRACK_POINTS = @{ value = 5 }
        RT_TRACK_AVERAGES = @{ value = $TrackingAverages }
        RT_COMMAND = @{ value = 1 }
        RT_COMMAND_SEQUENCE = @{ value = $sequence }
    }
    Send-Parameters $socket $settings
    $finished = $false
    while ($clock.Elapsed.TotalSeconds -lt 120) {
        $update = Receive-Update $socket $buffer $clock $writer
        if ($update -and $update.parameters) {
            foreach ($property in $update.parameters.PSObject.Properties) { $latest[$property.Name] = $property.Value.value }
        }
        if ($update -and $update.signals) {
            foreach ($property in $update.signals.PSObject.Properties) { $signals[$property.Name] = $property.Value.value }
        }
        if ([int]$latest.RT_COMMAND_ACK -lt $sequence) { continue }
        if ([int]$latest.RT_STATE -eq 10) {
            $finished = $true
            break
        }
        $signalSequence = @($signals['RT_BASELINE_SIGNAL_SEQUENCE'])
        $sensorIds = @($signals['RT_BASELINE_RESULT_SENSOR_ID'])
        if ([bool]$latest.RT_BASELINE_COMPLETE -and
            (-not [bool]$latest.RT_BASELINE_VALID -or
             ($signalSequence.Count -eq 1 -and [int]$signalSequence[0] -eq [int]$latest.RT_BASELINE_SEQUENCE -and
              $sensorIds.Count -eq [int]$latest.RT_RESONANCE_COUNT))) {
            $finished = $true
            break
        }
    }
    $writer.Flush()
    if (-not $finished) { throw 'Baseline did not complete within 120 seconds; inspect live state before retry.' }
    if (-not $latest.ContainsKey('RT_TRACK_AVERAGES') -or
        [int]$latest.RT_TRACK_AVERAGES -ne $TrackingAverages) {
        throw "RT_TRACK_AVERAGES readback mismatch; requested $TrackingAverages, received $($latest.RT_TRACK_AVERAGES). Deploy the updated app before this matrix."
    }
    $result = [pscustomobject]@{
        capture = $OutputPath; elapsed_s = $clock.Elapsed.TotalSeconds
        window_shift = $WindowShift; coarse_averages = $CoarseAverages
        refine_averages = $RefineAverages; tracking_averages = $TrackingAverages
        overview_points = 151
        command_ack = $latest.RT_COMMAND_ACK; state = $latest.RT_STATE
        baseline_sequence = $latest.RT_BASELINE_SEQUENCE
        baseline_complete = $latest.RT_BASELINE_COMPLETE
        baseline_valid = $latest.RT_BASELINE_VALID
        resonance_count = $latest.RT_RESONANCE_COUNT
        sensor_ids = $signals['RT_BASELINE_RESULT_SENSOR_ID']
        resonance_frequency_hz = $signals['RT_BASELINE_RESULT_FREQUENCY_HZ']
        resonance_q = $signals['RT_BASELINE_RESULT_Q']
        resonance_se_hz = $signals['RT_BASELINE_RESULT_SE_HZ']
        model_quality = $signals['RT_BASELINE_RESULT_MODEL_QUALITY']
        error = $latest.RT_ERROR
    }
    if ($TrackSeconds -gt 0 -and [bool]$latest.RT_BASELINE_VALID) {
        $modes = @(5)
        if ($IncludeThreePoint) { $modes += 3 }
        $runs = [Collections.Generic.List[object]]::new()
        foreach ($mode in $modes) {
            ++$sequence
            Send-Parameters $socket @{
                RT_TRACK_POINTS = @{ value = $mode }
                RT_COMMAND = @{ value = 5 }
                RT_COMMAND_SEQUENCE = @{ value = $sequence }
            }
            $start = $clock.Elapsed.TotalSeconds
            $firstSequence = $null
            $lastSequence = $null
            $rates = [Collections.Generic.List[double]]::new()
            while ($clock.Elapsed.TotalSeconds - $start -lt $TrackSeconds) {
                $update = Receive-Update $socket $buffer $clock $writer
                if ($update -and $update.parameters) {
                    foreach ($property in $update.parameters.PSObject.Properties) {
                        $latest[$property.Name] = $property.Value.value
                    }
                    if ([int]$latest.RT_COMMAND_ACK -ge $sequence -and [int]$latest.RT_TRACK_SEQUENCE -gt 0) {
                        if ($null -eq $firstSequence) { $firstSequence = [int]$latest.RT_TRACK_SEQUENCE }
                        $lastSequence = [int]$latest.RT_TRACK_SEQUENCE
                        if ([double]$latest.RT_TRACK_RATE_HZ -gt 0) {
                            $rates.Add([double]$latest.RT_TRACK_RATE_HZ)
                        }
                    }
                    if ([int]$latest.RT_STATE -eq 10) { throw "Tracking entered ERROR: $($latest.RT_ERROR)" }
                }
            }
            ++$sequence
            Send-Parameters $socket @{
                RT_COMMAND = @{ value = 6 }
                RT_COMMAND_SEQUENCE = @{ value = $sequence }
            }
            $stopDeadline = $clock.Elapsed.TotalSeconds + 10
            while ($clock.Elapsed.TotalSeconds -lt $stopDeadline) {
                $update = Receive-Update $socket $buffer $clock $writer
                if ($update -and $update.parameters) {
                    foreach ($property in $update.parameters.PSObject.Properties) {
                        $latest[$property.Name] = $property.Value.value
                    }
                    if ([int]$latest.RT_COMMAND_ACK -ge $sequence -and [int]$latest.RT_STATE -eq 3) { break }
                }
            }
            if ([int]$latest.RT_STATE -ne 3) { throw "Tracking stop did not return to BASELINE_READY: $($latest.RT_STATE)" }
            $runs.Add([pscustomobject]@{
                points = $mode; requested_duration_s = $TrackSeconds
                first_sequence = $firstSequence; last_sequence = $lastSequence
                mean_backend_rate_hz = if ($rates.Count) { ($rates | Measure-Object -Average).Average } else { $null }
            })
        }
        $result | Add-Member -NotePropertyName tracking_runs -NotePropertyValue @($runs)
    }
    $writer.Flush()
    $result | ConvertTo-Json -Compress -Depth 6
} finally {
    $writer.Dispose()
    $socket.Dispose()
}
