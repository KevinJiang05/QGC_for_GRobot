[CmdletBinding()]
param(
    [ValidateRange(1, 32)]
    [int]$Jobs = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildDirectory = Join-Path $projectRoot 'build-v5.1.5-debug'
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
$vsDevCmd = 'D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw 'Configure build-v5.1.5-debug first, following docs/debug/windows-v5.1.5-debug.md.'
}
if ((Get-Content -LiteralPath $cachePath -Raw) -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Debug\r?$') {
    throw 'The development build directory must be configured for Debug.'
}
$applicationPath = Join-Path $buildDirectory 'Debug\QGC_KevinJiang_v5_1_5_Debug.exe'
$applicationName = [IO.Path]::GetFileNameWithoutExtension($applicationPath)
$runningApplications = @(Get-Process -Name $applicationName -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $applicationPath })
if ($runningApplications.Count) {
    throw "Close the Debug application before rebuilding: $applicationPath"
}
if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
    throw "Visual Studio build environment is missing: $vsDevCmd"
}

$previousPath = $env:PATH
$timer = [System.Diagnostics.Stopwatch]::StartNew()
try {
    $env:PATH = '{0};{1};{2};{3}' -f 'D:\Develop\envs\Qt\6.11.1\msvc2022_64\bin',
                                       'D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64\bin',
                                       (Join-Path $projectRoot '.venv\Scripts'), $previousPath
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && cmake --build "{1}" --config Debug --parallel {2}' -f $vsDevCmd, $buildDirectory, $Jobs
    Push-Location -LiteralPath $projectRoot
    try {
        & $env:ComSpec /d /s /c $command
        if ($LASTEXITCODE -ne 0) {
            throw "Debug build failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:PATH = $previousPath
    $timer.Stop()
    Write-Host ('Debug build: {0} jobs, {1:N1} seconds.' -f $Jobs, $timer.Elapsed.TotalSeconds)
}
