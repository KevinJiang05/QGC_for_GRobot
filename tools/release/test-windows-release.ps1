# Contract tests use text fixtures and mocked native commands; no QGC build or NSIS execution.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testRoot = Join-Path $projectRoot ('.tmp\codex\windows-release-tests\' + [guid]::NewGuid().ToString('N'))
$pwsh = Join-Path $PSHOME 'pwsh.exe'
$releaseSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-windows-release.ps1') -Raw
$testCount = 0
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Write-FixtureFile {
    param([string]$Path, [string]$Content = 'test fixture only')
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Content)
}

function Invoke-TestProcess {
    param([string]$Executable, [string[]]$Arguments)
    $info = [Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) {
        $info.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        $process.Start() | Out-Null
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            $process.Kill($true)
            throw 'Workflow test process exceeded 30 seconds.'
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        }
    }
    finally {
        $process.Dispose()
    }
}

function New-ReleaseFixture {
    param([string]$Name)
    $root = Join-Path $testRoot $Name
    foreach ($file in @('CMakeLists.txt', 'deploy\windows\installheader.bmp',
                       'branding\GRobot_icons\GRobot_taskbar.ico', '.venv\Scripts\python.exe',
                       'tools\ai_detection\run_yolo_to_qgc_auto.py', 'tools\ai_detection\ai_detection_core.py',
                       'tools\ai_detection\yolo_to_qgc_udp.py',
                       'build-release\nsis-portable\nsis-3.11\Bin\makensis.exe')) {
        Write-FixtureFile (Join-Path $root $file)
    }
    Write-FixtureFile (Join-Path $root 'src\AppSettings\HelpSettings.qml') @'
import QtQuick
Item {
    property string versionText: "%1".arg(QGroundControl.qgcVersion)
}
'@
    Write-FixtureFile (Join-Path $root 'custom\cmake\CustomOverrides.cmake') @'
set(QGC_APP_VERSION_OVERRIDE "9.8.7")
set(QGC_APP_VERSION_STR_OVERRIDE "9.8.7")
'@
    Write-FixtureFile (Join-Path $root 'custom\GRobot.rc') @'
 FILEVERSION 9,8,7,0
 PRODUCTVERSION 9,8,7,0
 VALUE "FileVersion", "9.8.7"
 VALUE "ProductVersion", "9.8.7"
'@
    Write-FixtureFile (Join-Path $root 'deploy\windows\nullsoft_installer.nsi') (
        Get-Content -LiteralPath (Join-Path $projectRoot 'deploy\windows\nullsoft_installer.nsi') -Raw
    )
    return $root
}

$mockFunctions = @'
function Test-Path {
    [CmdletBinding()]
    param([string]$LiteralPath, [string]$PathType)
    if ($LiteralPath -like 'D:\Develop\envs\*' -or $LiteralPath -like 'D:\Develop\Toolchains\*') {
        return $true
    }
    return Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
function Get-Item {
    [CmdletBinding()]
    param([string]$LiteralPath)
    $item = Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath
    if ($LiteralPath.EndsWith('.exe')) {
        return [pscustomobject]@{
            FullName = $item.FullName
            Length = $item.Length
            VersionInfo = [pscustomobject]@{ ProductVersion = "$Version.0" }
        }
    }
    return $item
}
function Write-MockEvent {
    param([string]$Event)
    Add-Content -LiteralPath (Join-Path $projectRoot 'events.txt') -Value $Event
}
function Invoke-VsCommand {
    param([string]$Command)
    if ($Command -match 'cmake -S ') {
        Write-MockEvent 'configure'
        New-Item -ItemType Directory -Path (Join-Path $buildDirectory 'CMakeFiles') -Force | Out-Null
        $output = (Join-Path $buildDirectory 'fixture-installer.exe').Replace('\', '/')
        [IO.File]::WriteAllText((Join-Path $buildDirectory 'cmake_install.cmake'), "set(QGC_WINDOWS_OUT `"$output`")")
    }
    elseif ($Command -match 'cmake --build .* --parallel (\d+)') {
        Write-MockEvent "build:$($Matches[1])"
    }
    elseif ($Command -match '-DQGC_SKIP_WINDOWS_INSTALLER=ON') {
        Write-MockEvent 'stage'
        foreach ($file in @('bin\QGC_KevinJiang.exe', 'plugins\platforms\qwindows.dll',
                           'bin\Qt6Core.dll', 'bin\Qt6Gui.dll', 'bin\Qt6Qml.dll', 'bin\Qt6Quick.dll',
                           'bin\gstgl-1.0-0.dll', 'bin\gstreamer-1.0-0.dll',
                           'bin\ai_detection\run_yolo_to_qgc_auto.py',
                           'bin\ai_detection\ai_detection_core.py', 'bin\ai_detection\yolo_to_qgc_udp.py')) {
            if ($script:FixtureFailure -eq 'runtime' -and $file -eq 'plugins\platforms\qwindows.dll') { continue }
            $path = Join-Path $stagingDirectory $file
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            [IO.File]::WriteAllText($path, 'test fixture only')
        }
    }
    elseif ($Command -match '--component windows-installer') {
        Write-MockEvent 'package'
        [IO.File]::WriteAllText((Join-Path $buildDirectory 'fixture-installer.exe'), 'text fixture; not an installer')
    }
    else {
        throw "Unexpected native command in fixture: $Command"
    }
}
function Invoke-StagedBootTest {
    param([string]$ApplicationPath)
    Write-MockEvent 'boot'
    if ($script:FixtureFailure -eq 'boot') { throw 'Simulated staged boot failure' }
}
function Get-AuthenticodeSignature {
    param([string]$LiteralPath)
    Write-MockEvent 'signature'
    if ($script:FixtureFailure -eq 'verify') { throw 'Simulated final verification failure' }
    return [pscustomobject]@{ Status = 'NotSigned' }
}
'@

function Invoke-ReleaseFixture {
    param([string]$Root, [string]$Failure = '', [string[]]$Arguments = @())
    $marker = '$script:StartedAt = (Get-Date).ToString(''o'')'
    Assert-True ($releaseSource.Contains($marker)) 'Release entrypoint marker was not found.'
    $injected = '$script:FixtureFailure = ''' + $Failure + "'`n" + $mockFunctions + "`n"
    $scriptPath = Join-Path $Root 'tools\release\build-windows-release.ps1'
    Write-FixtureFile $scriptPath ($releaseSource.Replace($marker, $injected + $marker))
    $result = Invoke-TestProcess $pwsh (@('-NoProfile', '-File', $scriptPath, '-Version', '9.8.7') + $Arguments)
    $auditRoot = Join-Path $Root 'dist\QGC_KevinJiang_v9.8.7\release-audit'
    $stateFiles = @(Get-ChildItem -LiteralPath $auditRoot -Filter release-state.json -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName)
    return [pscustomobject]@{
        ExitCode = $result.ExitCode
        Output = $result.Output
        State = $(if ($stateFiles.Count) { Get-Content -LiteralPath $stateFiles[-1].FullName -Raw | ConvertFrom-Json } else { $null })
        AuditDirectory = $(if ($stateFiles.Count) { $stateFiles[-1].DirectoryName } else { $null })
    }
}

function Complete-Test {
    param([string]$Name)
    $script:testCount++
    Write-Host "PASS: $Name"
}

$fixture = New-ReleaseFixture 'preflight'
$result = Invoke-ReleaseFixture $fixture -Arguments @('-PreflightOnly')
Assert-True ($result.ExitCode -eq 0 -and $result.State.status -eq 'preflight-succeeded') $result.Output
Assert-True (-not (Test-Path (Join-Path $fixture 'events.txt'))) 'Preflight executed a native build or boot command.'
Assert-True (-not (Test-Path $result.State.stagingDirectory)) 'Preflight created staging.'
Complete-Test 'preflight leaves versions, build and staging untouched'

$fixture = New-ReleaseFixture 'success'
$versionPath = Join-Path $fixture 'custom\cmake\CustomOverrides.cmake'
$versionTime = (Get-Item -LiteralPath $versionPath).LastWriteTimeUtc
$result = Invoke-ReleaseFixture $fixture
Assert-True ($result.ExitCode -eq 0 -and $result.State.status -eq 'succeeded') $result.Output
Assert-True ($result.State.steps.Count -eq 8) 'Normal release did not complete eight steps.'
$events = (Get-Content -LiteralPath (Join-Path $fixture 'events.txt')) -join ','
Assert-True ($events -eq 'configure,build:8,stage,boot,package,signature') "Wrong phase order: $events"
Assert-True ((Get-Item -LiteralPath $versionPath).LastWriteTimeUtc -eq $versionTime) 'Unchanged version source was rewritten.'
Complete-Test 'eight-job build, boot before packaging, no repeated deployment or version rewrite'

$fixture = New-ReleaseFixture 'include-prefix'
Write-FixtureFile (Join-Path $fixture 'build-v5.1.5-release\.ninja_deps') 'original dependency log'
foreach ($language in @('C', 'CXX')) {
    Write-FixtureFile (Join-Path $fixture "build-v5.1.5-release\CMakeFiles\4.1.0\CMake${language}Compiler.cmake") (
        'set(CMAKE_' + $language + '_CL_SHOWINCLUDES_PREFIX "娉ㄦ剰: 鍖呭惈鏂囦欢:  ")'
    )
}
$result = Invoke-ReleaseFixture $fixture
Assert-True ($result.ExitCode -eq 0) $result.Output
$events = (Get-Content -LiteralPath (Join-Path $fixture 'events.txt')) -join ','
Assert-True ($events -eq 'configure,configure,build:8,stage,boot,package,signature') "Wrong prefix repair order: $events"
foreach ($language in @('C', 'CXX')) {
    $compilerContent = Get-Content -LiteralPath (Join-Path $fixture "build-v5.1.5-release\CMakeFiles\4.1.0\CMake${language}Compiler.cmake") -Raw
    Assert-True ($compilerContent.Contains('"注意: 包含文件: "')) 'Ninja include prefix was not repaired.'
}
$savedDependencyLogs = @(Get-ChildItem -LiteralPath (Join-Path $fixture 'build-v5.1.5-release') -Filter '.ninja_deps.before-prefix-repair-*')
Assert-True ($savedDependencyLogs.Count -eq 1 -and
    (Get-Content -LiteralPath $savedDependencyLogs[0].FullName -Raw) -eq 'original dependency log') 'Original dependency log was not preserved.'
Complete-Test 'Chinese MSVC include-prefix repair regenerates Ninja before compilation'

$fixture = New-ReleaseFixture 'correct-include-prefix'
Write-FixtureFile (Join-Path $fixture 'build-v5.1.5-release\.ninja_deps') 'valid dependency log'
Write-FixtureFile (Join-Path $fixture 'build-v5.1.5-release\CMakeFiles\4.1.0\CMakeCXXCompiler.cmake') (
    'set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件: ")'
)
$result = Invoke-ReleaseFixture $fixture
Assert-True ($result.ExitCode -eq 0) $result.Output
$events = (Get-Content -LiteralPath (Join-Path $fixture 'events.txt')) -join ','
Assert-True ($events -eq 'configure,build:8,stage,boot,package,signature') 'Correct compiler prefix caused repeated configuration.'
Assert-True ((Get-Content -LiteralPath (Join-Path $fixture 'build-v5.1.5-release\.ninja_deps') -Raw) -eq 'valid dependency log') 'Valid dependency tracking was reset.'
Complete-Test 'valid include prefix preserves incremental dependencies without extra configure'

foreach ($failure in @('boot', 'runtime')) {
    $fixture = New-ReleaseFixture $failure
    $result = Invoke-ReleaseFixture $fixture -Failure $failure -Arguments @('-Jobs', '16')
    Assert-True ($result.ExitCode -ne 0 -and $result.State.steps[-1].name -eq 'Verify staged application') $result.Output
    Assert-True (-not (Test-Path -LiteralPath $result.State.installerPath)) 'Installer was produced after failed staging verification.'
    Complete-Test "$failure failure stops before compression"
}

$fixture = New-ReleaseFixture 'verify-retry'
$result = Invoke-ReleaseFixture $fixture -Failure 'verify' -Arguments @('-Jobs', '12')
Assert-True ($result.ExitCode -ne 0 -and $result.State.steps[-1].name -eq 'Verify release artifact') $result.Output
$originalAudit = $result.AuditDirectory
$originalState = Get-Content -LiteralPath (Join-Path $originalAudit 'release-state.json') -Raw
$beforeEvents = @(Get-Content -LiteralPath (Join-Path $fixture 'events.txt'))
$result = Invoke-ReleaseFixture $fixture -Arguments @('-VerifyOnly', '-AuditDirectory', $originalAudit)
Assert-True ($result.ExitCode -eq 0 -and $result.State.status -eq 'succeeded') $result.Output
Assert-True ($result.State.mode -eq 'verification' -and $result.State.compilerJobs -eq 12) 'Retry audit lost its source configuration.'
Assert-True ($result.State.sourceAudit -eq (Join-Path $originalAudit 'release-state.json')) 'Retry did not link its source audit.'
Assert-True ((Get-Content -LiteralPath (Join-Path $originalAudit 'release-state.json') -Raw) -eq $originalState) 'Retry overwrote original evidence.'
$afterEvents = @(Get-Content -LiteralPath (Join-Path $fixture 'events.txt'))
Assert-True (($afterEvents | Select-Object -Skip $beforeEvents.Count) -join ',' -eq 'boot,signature') 'Retry rebuilt, deployed or packaged.'
Complete-Test 'failed final verification retries independently and preserves original audit'

foreach ($changedFile in @('installer', 'application', 'nsis')) {
    $fixture = New-ReleaseFixture "changed-$changedFile"
    $result = Invoke-ReleaseFixture $fixture
    Assert-True ($result.ExitCode -eq 0) $result.Output
    $originalAudit = $result.AuditDirectory
    $changedPath = switch ($changedFile) {
        'installer' { $result.State.installerPath }
        'application' { Join-Path $result.State.stagingDirectory 'bin\QGC_KevinJiang.exe' }
        'nsis' { Join-Path $fixture 'deploy\windows\nullsoft_installer.nsi' }
    }
    Add-Content -LiteralPath $changedPath -Value 'changed fixture'
    $result = Invoke-ReleaseFixture $fixture -Arguments @('-VerifyOnly', '-AuditDirectory', $originalAudit)
    Assert-True ($result.ExitCode -ne 0 -and $result.State.failure -match 'Release input changed') $result.Output
    Complete-Test "verification rejects changed $changedFile"
}

$fixture = New-ReleaseFixture 'incomplete'
$result = Invoke-ReleaseFixture $fixture -Failure 'boot'
$result = Invoke-ReleaseFixture $fixture -Arguments @('-VerifyOnly', '-AuditDirectory', $result.AuditDirectory)
Assert-True ($result.ExitCode -ne 0 -and $result.State.failure -match 'completed staging') $result.Output
Complete-Test 'verification refuses a release that never completed staging'

$fixture = New-ReleaseFixture 'running-audit'
$result = Invoke-ReleaseFixture $fixture
Assert-True ($result.ExitCode -eq 0) $result.Output
$originalAudit = $result.AuditDirectory
$result.State.status = 'running'
$result.State.steps[-1].status = 'running'
Write-FixtureFile (Join-Path $originalAudit 'release-state.json') ($result.State | ConvertTo-Json -Depth 8)
$result = Invoke-ReleaseFixture $fixture -Arguments @('-VerifyOnly', '-AuditDirectory', $originalAudit)
Assert-True ($result.ExitCode -ne 0 -and $result.State.failure -match 'completed staging') $result.Output
Complete-Test 'verification refuses an audit still in progress'

$fixture = New-ReleaseFixture 'invalid-jobs'
$result = Invoke-ReleaseFixture $fixture -Arguments @('-Jobs', '0')
Assert-True ($result.ExitCode -ne 0 -and $null -eq $result.State) 'Invalid job count started a release.'
Complete-Test 'invalid job count is rejected before pipeline execution'

# Exercise the real Windows install block and CreateWinInstaller.cmake. Only the
# final execute_process command is replaced, so component dispatch and skip
# behavior use real CMake-generated install rules.
$cmakeFixture = Join-Path $testRoot 'cmake-components'
$installSource = Get-Content -LiteralPath (Join-Path $projectRoot 'cmake\install\Install.cmake') -Raw
$blockStart = $installSource.IndexOf('elseif(WIN32)')
$blockEnd = $installSource.IndexOf('elseif(MACOS)', $blockStart)
$windowsBlock = $installSource.Substring($blockStart, $blockEnd - $blockStart).Replace('elseif(WIN32)', 'if(WIN32)') + "`nendif()`n"
Write-FixtureFile (Join-Path $cmakeFixture 'payload.marker') 'deployment fixture'
Write-FixtureFile (Join-Path $cmakeFixture 'cmake\install\CreateWinInstaller.cmake') (
    Get-Content -LiteralPath (Join-Path $projectRoot 'cmake\install\CreateWinInstaller.cmake') -Raw
)
$cmakeHeader = @'
cmake_minimum_required(VERSION 3.25)
project(QGCFixture VERSION 9.8.7 LANGUAGES NONE)
set(QGC_ORG_NAME Fixture)
set(QGC_WINDOWS_ICON_PATH "${CMAKE_SOURCE_DIR}/absent.ico")
set(QGC_WINDOWS_INSTALL_HEADER_PATH "${CMAKE_SOURCE_DIR}/absent.bmp")
set(CMAKE_SYSTEM_PROCESSOR AMD64)
install(FILES payload.marker DESTINATION bin)
install(CODE [[
    set(QGC_NSIS_INSTALLER_CMD "${CMAKE_COMMAND}")
    function(execute_process)
        file(APPEND "${CMAKE_INSTALL_PREFIX}/nsis-calls.txt" "${ARGV}\n")
    endfunction()
]] COMPONENT windows-installer)
'@
Write-FixtureFile (Join-Path $cmakeFixture 'CMakeLists.txt') ($cmakeHeader + "`n" + $windowsBlock)
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$cmakeBuild = Join-Path $cmakeFixture 'build'
$cmakeStage = Join-Path $cmakeFixture 'stage'
$result = Invoke-TestProcess $cmake @('-S', $cmakeFixture, '-B', $cmakeBuild)
Assert-True ($result.ExitCode -eq 0) $result.Output
$result = Invoke-TestProcess $cmake @("-DCMAKE_INSTALL_PREFIX=$cmakeStage", '-DCMAKE_INSTALL_CONFIG_NAME=Release',
    '-DQGC_SKIP_WINDOWS_INSTALLER=ON', '-P', (Join-Path $cmakeBuild 'cmake_install.cmake'))
Assert-True ($result.ExitCode -eq 0 -and (Test-Path (Join-Path $cmakeStage 'bin\payload.marker'))) $result.Output
Assert-True (-not (Test-Path (Join-Path $cmakeStage 'nsis-calls.txt'))) 'Staging called NSIS.'
Write-FixtureFile (Join-Path $cmakeStage 'bin\payload.marker') 'preserve this staged payload'
$result = Invoke-TestProcess $cmake @('--install', $cmakeBuild, '--config', 'Release', '--prefix', $cmakeStage,
    '--component', 'windows-installer')
Assert-True ($result.ExitCode -eq 0 -and (Test-Path (Join-Path $cmakeStage 'nsis-calls.txt'))) $result.Output
Assert-True ((Get-Content (Join-Path $cmakeStage 'bin\payload.marker') -Raw) -eq 'preserve this staged payload') 'Installer component repeated deployment.'
Complete-Test 'real CMake staging defers NSIS; installer component preserves staged payload'

$fullStage = Join-Path $cmakeFixture 'full-stage'
$result = Invoke-TestProcess $cmake @('--install', $cmakeBuild, '--config', 'Release', '--prefix', $fullStage)
Assert-True ($result.ExitCode -eq 0 -and (Test-Path (Join-Path $fullStage 'bin\payload.marker')) -and
    (Test-Path (Join-Path $fullStage 'nsis-calls.txt'))) $result.Output
Complete-Test 'ordinary cmake --install still deploys and invokes installer generation'

$debugFixture = Join-Path $testRoot 'debug-entrypoint'
$debugPath = Join-Path $debugFixture 'tools\debug\build-windows-debug.ps1'
Write-FixtureFile $debugPath (Get-Content -LiteralPath (Join-Path $projectRoot 'tools\debug\build-windows-debug.ps1') -Raw)
$result = Invoke-TestProcess $pwsh @('-NoProfile', '-File', $debugPath)
Assert-True ($result.ExitCode -ne 0 -and $result.Output -match 'Configure build-v5.1.5-debug first') $result.Output
Write-FixtureFile (Join-Path $debugFixture 'build-v5.1.5-debug\CMakeCache.txt') 'CMAKE_BUILD_TYPE:STRING=Release'
$result = Invoke-TestProcess $pwsh @('-NoProfile', '-File', $debugPath)
Assert-True ($result.ExitCode -ne 0 -and $result.Output -match 'must be configured for Debug') $result.Output
Complete-Test 'Debug entrypoint refuses missing or Release configuration without building'

Write-FixtureFile (Join-Path $debugFixture 'build-v5.1.5-debug\CMakeCache.txt') 'CMAKE_BUILD_TYPE:STRING=Debug'
$debugSource = Get-Content -LiteralPath $debugPath -Raw
$processMock = @'
function Get-Process {
    [CmdletBinding()]
    param([string]$Name)
    return [pscustomobject]@{ Path = $applicationPath }
}
'@
$debugMarker = '$applicationPath = Join-Path $buildDirectory'
Write-FixtureFile $debugPath ($debugSource.Replace($debugMarker, $processMock + "`n" + $debugMarker))
$result = Invoke-TestProcess $pwsh @('-NoProfile', '-File', $debugPath)
Assert-True ($result.ExitCode -ne 0 -and $result.Output -match 'Close the Debug application before rebuilding') $result.Output
Complete-Test 'Debug entrypoint detects its running executable before compilation'

Write-Host "$testCount workflow tests passed. Fixtures: $testRoot"
