param([Parameter(Mandatory = $true)][string]$InputPath)
$ErrorActionPreference = 'Stop'
$messages = 0
$trackerMessages = 0
$names = [Collections.Generic.HashSet[string]]::new()
$last = @{}
foreach ($line in [IO.File]::ReadLines((Resolve-Path -LiteralPath $InputPath).Path)) {
    $record = $line | ConvertFrom-Json
    $bytes = [Convert]::FromBase64String($record.data_base64)
    ++$messages
    if ($bytes.Length -lt 5 -or [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'EZIA') { continue }
    $inputStream = [IO.MemoryStream]::new($bytes, 4, $bytes.Length - 4)
    $decompressor = [IO.Compression.GzipStream]::new($inputStream, [IO.Compression.CompressionMode]::Decompress)
    $outputStream = [IO.MemoryStream]::new()
    $decompressor.CopyTo($outputStream)
    $update = [Text.Encoding]::UTF8.GetString($outputStream.ToArray()) | ConvertFrom-Json
    $hasTracker = $false
    if ($update.parameters) {
        foreach ($property in $update.parameters.PSObject.Properties) {
            if ($property.Name.StartsWith('RT_')) {
                $hasTracker = $true
                [void]$names.Add($property.Name)
                $last[$property.Name] = $property.Value.value
            }
        }
    }
    if ($hasTracker) { ++$trackerMessages }
}
[pscustomobject]@{
    file = $InputPath
    messages = $messages
    tracker_parameter_messages = $trackerMessages
    tracker_parameter_count = $names.Count
    last_state = $last['RT_STATE']
    last_error = $last['RT_ERROR']
    last_command_ack = $last['RT_COMMAND_ACK']
} | ConvertTo-Json -Compress
