param(
    [string]$HostName = "127.0.0.1",
    [int]$Port = 15020,
    [string]$GatewayId = "GW-DTU-01",
    [string]$DeviceId = "DEV-DTU-01",
    [double]$Temperature = 25.6,
    [double]$Humidity = 62.3
)

$payload = @{
    gatewayId   = $GatewayId
    gatewayName = "4G DTU 透传网关"
    deviceId    = $DeviceId
    deviceName  = "1号温湿度探头"
    location    = "管廊东段"
    temperature = $Temperature
    humidity    = $Humidity
} | ConvertTo-Json -Compress

$client = New-Object System.Net.Sockets.TcpClient
$client.Connect($HostName, $Port)
$stream = $client.GetStream()
$bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
$stream.Write($bytes, 0, $bytes.Length)
$stream.Flush()
$stream.Dispose()
$client.Dispose()

Write-Host "Sent DTU payload to $HostName`:$Port"
Write-Host $payload
