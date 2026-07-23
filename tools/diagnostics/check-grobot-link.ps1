[CmdletBinding()]
param(
    [switch]$ProbePort,
    [string]$LocalAddress = "192.168.1.5",
    [string]$DeviceAddress = "192.168.1.200",
    [int]$Port = 4019
)

$ErrorActionPreference = "Stop"

$qgcProcesses = @(Get-Process -Name "QGC_KevinJiang" -ErrorAction SilentlyContinue)
$qgcProcessIds = @($qgcProcesses | ForEach-Object { $_.Id })
$establishedSessions = @(
    Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue |
        Where-Object {
            ($qgcProcessIds -contains $_.OwningProcess) -and
            ($_.RemoteAddress -eq $DeviceAddress) -and
            ($_.RemotePort -eq $Port)
        }
)

$pingSucceeded = Test-Connection -ComputerName $DeviceAddress -Count 1 -Quiet
$portReachable = $null
$probeError = $null

# An established QGC session is stronger evidence than a new connection probe.
# Do not open a second TCP client unless the operator explicitly requests it.
if (($establishedSessions.Count -eq 0) -and $ProbePort) {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $client.Client.Bind(
            [System.Net.IPEndPoint]::new(
                [System.Net.IPAddress]::Parse($LocalAddress),
                0
            )
        )
        $connectTask = $client.ConnectAsync($DeviceAddress, $Port)
        $portReachable = $connectTask.Wait(1500) -and $client.Connected
    } catch {
        $portReachable = $false
        $probeError = $_.Exception.Message
    } finally {
        $client.Dispose()
    }
}

$status = if ($establishedSessions.Count -gt 0) {
    "QGC_TCP_ESTABLISHED"
} elseif (-not $pingSucceeded) {
    "DEVICE_UNREACHABLE"
} elseif (-not $ProbePort) {
    "NO_QGC_SESSION_PROBE_SKIPPED"
} elseif ($portReachable) {
    "TCP_SERVER_REACHABLE_QGC_DISCONNECTED"
} else {
    "TCP_SERVER_UNREACHABLE"
}

[pscustomobject]@{
    Status             = $status
    DeviceAddress      = $DeviceAddress
    LocalAddress       = $LocalAddress
    Port               = $Port
    PingSucceeded      = $pingSucceeded
    QgcProcessIds      = $qgcProcessIds -join ","
    QgcSessionCount    = $establishedSessions.Count
    PortProbeRequested = [bool]$ProbePort
    PortReachable      = $portReachable
    ProbeError         = $probeError
}

