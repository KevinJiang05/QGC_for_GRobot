[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-v5.1.5-debug',
    [string]$QtRoot = 'D:\Develop\envs\Qt\6.11.1\msvc2022_64',
    [string]$GStreamerRoot = 'D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64',
    [switch]$Diagnostics,
    [string[]]$AppArgs = @()
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot $BuildDirectory
}
$executable = Join-Path $BuildDirectory 'Debug\QGC_KevinJiang_v5_1_5_Debug.exe'
$scanner = Join-Path $GStreamerRoot 'libexec\gstreamer-1.0\gst-plugin-scanner.exe'
foreach ($requiredPath in @($executable, (Join-Path $QtRoot 'bin\Qt6Cored.dll'), $scanner)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Missing Debug runtime file: $requiredPath"
    }
}

$runtimeCache = Join-Path $BuildDirectory 'runtime-cache'
New-Item -ItemType Directory -Path $runtimeCache -Force | Out-Null
$pluginDirectory = Join-Path $GStreamerRoot 'lib\gstreamer-1.0'
$runtimeEnvironment = @{
    PATH = "$(Join-Path $QtRoot 'bin');$(Join-Path $GStreamerRoot 'bin');$(Join-Path $GStreamerRoot 'lib\libproxy');$env:PATH"
    QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
    QML_IMPORT_PATH = Join-Path $QtRoot 'qml'
    GST_PLUGIN_PATH_1_0 = $pluginDirectory
    GST_PLUGIN_SYSTEM_PATH_1_0 = $pluginDirectory
    GST_PLUGIN_SCANNER_1_0 = $scanner
    GST_REGISTRY_1_0 = Join-Path $runtimeCache 'gstreamer-1.28.4-registry.bin'
    GIO_MODULE_DIR = Join-Path $GStreamerRoot 'lib\gio\modules'
    SSL_CERT_FILE = Join-Path $GStreamerRoot 'etc\ssl\certs\ca-certificates.crt'
}
$previousEnvironment = @{}
if ($Diagnostics) {
    $AppArgs += @('--logging:VideoAllLog,qgc.deepshark.videocontroller', '--log-output')
}
try {
    foreach ($entry in $runtimeEnvironment.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    # Run directly against the shared SDKs; no deployment, installer or global environment changes.
    $ErrorActionPreference = 'Continue'
    & $executable @AppArgs | Out-Host
    $applicationExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
} finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
}
exit $applicationExitCode
