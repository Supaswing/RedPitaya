param(
    [string]$HostName = 'rp-f0f8d5.local',
    [int]$DurationSeconds = 20,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
if ($DurationSeconds -lt 1 -or $DurationSeconds -gt 300) { throw 'Duration must be 1-300 seconds.' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
$socket = [Net.WebSockets.ClientWebSocket]::new()
$uri = [Uri]::new("ws://$HostName/wss")
$socket.ConnectAsync($uri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
$request = [Text.Encoding]::UTF8.GetBytes('{"parameters":{"in_command":{"value":"send_all_params"}}}')
$socket.SendAsync([ArraySegment[byte]]::new($request), [Net.WebSockets.WebSocketMessageType]::Text,
                  $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
$stream = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew)
$writer = [IO.StreamWriter]::new($stream, [Text.Encoding]::UTF8)
$clock = [Diagnostics.Stopwatch]::StartNew()
$buffer = New-Object byte[] 65536
$count = 0
try {
    while ($clock.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $message = [IO.MemoryStream]::new()
        $kind = ''
        do {
            $deadline = [Threading.CancellationTokenSource]::new(5000)
            try {
                $part = $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $deadline.Token).GetAwaiter().GetResult()
            } finally { $deadline.Dispose() }
            if ($part.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { break }
            $kind = $part.MessageType.ToString()
            $message.Write($buffer, 0, $part.Count)
        } while (-not $part.EndOfMessage)
        if ($part.MessageType -eq [Net.WebSockets.WebSocketMessageType]::Close) { break }
        $record = [pscustomobject]@{
            utc = [DateTime]::UtcNow.ToString('o')
            elapsed_s = $clock.Elapsed.TotalSeconds
            type = $kind
            data_base64 = [Convert]::ToBase64String($message.ToArray())
        }
        $writer.WriteLine(($record | ConvertTo-Json -Compress))
        ++$count
    }
} finally {
    $writer.Dispose()
    $socket.Dispose()
}
Write-Host "Captured $count WebSocket messages in $($clock.Elapsed.TotalSeconds.ToString('F2')) s to $OutputPath"
