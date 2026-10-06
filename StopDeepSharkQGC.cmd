@echo off
setlocal

set "DEEPSHARK_STOP_ARGS=%*"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p='%~f0'; $s=Get-Content -Raw -LiteralPath $p; $m=('# ' + 'POWERSHELL'); $i=$s.IndexOf($m); if ($i -lt 0) { exit 1 }; Invoke-Expression $s.Substring($i)"
exit /b %ERRORLEVEL%

# POWERSHELL
$ErrorActionPreference = 'SilentlyContinue'

$self = $PID
$dryRun = $env:DEEPSHARK_STOP_ARGS -match '(^|\s)(/list|--list|/dry-run|--dry-run)(\s|$)'
$all = @(Get-CimInstance Win32_Process)
$rootIds = New-Object 'System.Collections.Generic.HashSet[int]'

foreach ($p in $all) {
    if ($p.ProcessId -eq $self) {
        continue
    }

    $name = [string]$p.Name
    $cmd = [string]$p.CommandLine
    $exe = [string]$p.ExecutablePath

    $matches =
        ($name -in @('QGC_KevinJiang.exe', 'QGC_KevinJiang_v5_1_5_Debug.exe')) -or
        ($name -eq 'mediamtx.exe' -and ($cmd -like '*mediamtx-deepshark.yml*' -or $exe -like '*QGC_for_GRobot*tools*rtsp*')) -or
        ($name -eq 'ffmpeg.exe' -and $cmd -like '*rtsp://127.0.0.1:8554/deepshark*') -or
        ($name -eq 'python.exe' -and ($cmd -like '*deep_shark_studio.qgc.runtime_service*' -or $cmd -like '*DeepSharkViewStudio*app.py*' -or $exe -like '*deep-shark-view-studio*')) -or
        ($name -eq 'cmd.exe' -and ($cmd -like '*StartDeepSharkQGC.cmd*' -or $cmd -like '*StartDeepSharkRTSP.cmd*' -or $cmd -like '*StartDeepSharkViewStudio.cmd*' -or $cmd -like '*mediamtx-deepshark.yml*' -or $cmd -like '*QGC_KevinJiang.exe*' -or $cmd -like '*QGC_KevinJiang_v5_1_5_Debug.exe*'))

    if ($matches) {
        [void]$rootIds.Add([int]$p.ProcessId)
    }
}

$changed = $true
while ($changed) {
    $changed = $false
    foreach ($p in $all) {
        if ($p.ProcessId -ne $self -and $rootIds.Contains([int]$p.ParentProcessId) -and -not $rootIds.Contains([int]$p.ProcessId)) {
            [void]$rootIds.Add([int]$p.ProcessId)
            $changed = $true
        }
    }
}

$targets = @($all | Where-Object { $rootIds.Contains([int]$_.ProcessId) } | Sort-Object ProcessId -Descending)

if (-not $targets) {
    Write-Host 'No DeepShark/QGC service processes found.'
    exit 0
}

Write-Host 'Stopping DeepShark/QGC service processes:'
foreach ($p in $targets) {
    Write-Host ('  PID {0,7}  {1}' -f $p.ProcessId, $p.Name)
}

if ($dryRun) {
    Write-Host 'Dry run only. Run StopDeepSharkQGC.cmd without /list to stop them.'
    exit 0
}

foreach ($p in $targets) {
    Stop-Process -Id $p.ProcessId -Force
}

Start-Sleep -Milliseconds 800

$remaining = @(Get-CimInstance Win32_Process | Where-Object { $rootIds.Contains([int]$_.ProcessId) })
if ($remaining) {
    Write-Host 'Some processes are still running:'
    foreach ($p in $remaining) {
        Write-Host ('  PID {0,7}  {1}' -f $p.ProcessId, $p.Name)
    }
    exit 1
}

Write-Host 'All DeepShark/QGC service processes have been stopped.'
exit 0
