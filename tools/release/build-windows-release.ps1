[CmdletBinding(DefaultParameterSetName = 'Release')]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [Parameter(ParameterSetName = 'Preflight', Mandatory = $true)]
    [switch]$PreflightOnly,

    [Parameter(ParameterSetName = 'Verify', Mandatory = $true)]
    [switch]$VerifyOnly,

    [Parameter(ParameterSetName = 'Verify', Mandatory = $true)]
    [string]$AuditDirectory,

    [Parameter(ParameterSetName = 'Release')]
    [Parameter(ParameterSetName = 'Preflight')]
    [ValidateRange(1, 32)]
    [int]$Jobs = 8,

    [Parameter(ParameterSetName = 'Release')]
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildDirectory = Join-Path $projectRoot 'build-v5.1.5-release'
$qtRoot = 'D:\Develop\envs\Qt\6.11.1\msvc2022_64'
$gstreamerRoot = 'D:\Develop\envs\GStreamer\1.28.4\msvc_x86_64'
$pythonExecutable = Join-Path $projectRoot '.venv\Scripts\python.exe'
$distributionDirectory = Join-Path $projectRoot ("dist\QGC_KevinJiang_v{0}" -f $Version)
$installerName = "QGC_v{0}_KevinJiang-installer.exe" -f $Version
$installerPath = Join-Path $distributionDirectory $installerName
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runDirectory = Join-Path $distributionDirectory ("release-audit\{0}" -f $runId)
$logDirectory = Join-Path $runDirectory 'steps'
$stagingDirectory = Join-Path $buildDirectory ("release-runs\v{0}\{1}\staging" -f $Version, $runId)
$statePath = Join-Path $runDirectory 'release-state.json'
$reportPath = Join-Path $runDirectory 'release-report.md'

New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
if (-not $VerifyOnly -and -not $PreflightOnly) {
    New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null
}

$script:RunStatus = 'running'
$script:FailureMessage = $null
$script:StepNumber = 0
$script:Steps = [System.Collections.Generic.List[object]]::new()
$script:Artifact = $null
$script:NsisPath = $null
$script:Candidate = $null
$script:SourceAudit = $null
$script:Mode = $(if ($PreflightOnly) { 'preflight' } elseif ($VerifyOnly) { 'verification' } else { 'release' })
$script:StepCount = $(if ($PreflightOnly -or $VerifyOnly) { 1 } else { 8 })

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$LiteralPath,
        [Parameter(Mandatory = $true)][string]$Content
    )

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($LiteralPath, $Content, $encoding)
}

function Write-RunState {
    $state = [ordered]@{
        schemaVersion = 2
        application = 'QGC_KevinJiang'
        requestedVersion = $Version
        runId = $runId
        mode = $script:Mode
        compilerJobs = $Jobs
        sourceAudit = $script:SourceAudit
        status = $script:RunStatus
        startedAt = $script:StartedAt
        updatedAt = (Get-Date).ToString('o')
        projectRoot = $projectRoot
        buildDirectory = $buildDirectory
        stagingDirectory = $stagingDirectory
        distributionDirectory = $distributionDirectory
        installerPath = $installerPath
        nsisPath = $script:NsisPath
        failure = $script:FailureMessage
        steps = @($script:Steps)
        artifact = $script:Artifact
        candidate = $script:Candidate
    }

    Write-Utf8NoBom -LiteralPath $statePath -Content ($state | ConvertTo-Json -Depth 8)
}

function Invoke-ReleaseStep {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    $script:StepNumber++
    $safeName = $Name.ToLowerInvariant() -replace '[^a-z0-9]+', '-'
    $logPath = Join-Path $logDirectory ("{0:D2}-{1}.log" -f $script:StepNumber, $safeName.Trim('-'))
    $started = Get-Date
    $record = [pscustomobject][ordered]@{
        number = $script:StepNumber
        name = $Name
        status = 'running'
        startedAt = $started.ToString('o')
        finishedAt = $null
        durationSeconds = $null
        logPath = $logPath
        error = $null
    }
    $script:Steps.Add($record)
    Write-RunState

    Start-Transcript -LiteralPath $logPath -Force | Out-Null
    try {
        Write-Host ("[{0}/{1}] {2}" -f $script:StepNumber, $script:StepCount, $Name)
        & $Action
        $record.status = 'succeeded'
    }
    catch {
        $record.status = 'failed'
        $record.error = $_.Exception.Message
        Write-Error $_.Exception.Message
        throw
    }
    finally {
        $finished = Get-Date
        $record.finishedAt = $finished.ToString('o')
        $record.durationSeconds = [math]::Round(($finished - $started).TotalSeconds, 3)
        Stop-Transcript | Out-Null
        Write-RunState
    }
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    Write-Host ("Command: {0} {1}" -f $Executable, ($Arguments -join ' '))
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable"
    }
}

function Invoke-VsCommand {
    param([Parameter(Mandatory = $true)][string]$Command)

    $vsDevCmd = 'D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat'
    $wrapped = 'call "{0}" -arch=x64 -host_arch=x64 >nul && {1}' -f $vsDevCmd, $Command
    Invoke-NativeCommand -Executable $env:ComSpec -Arguments @('/d', '/s', '/c', $wrapped)
}

function Invoke-StagedBootTest {
    param([Parameter(Mandatory = $true)][string]$ApplicationPath)

    $stagedBin = Split-Path -Parent $ApplicationPath
    $stagedRoot = Split-Path -Parent $stagedBin
    $process = $null

    try {
        $systemRoot = [Environment]::GetEnvironmentVariable('SystemRoot', 'Process')
        $cleanPath = @($stagedBin, (Join-Path $systemRoot 'System32'), $systemRoot) -join ';'

        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $ApplicationPath
        $startInfo.Arguments = '--simple-boot-test'
        $startInfo.WorkingDirectory = $stagedBin
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $startInfo.EnvironmentVariables['PATH'] = $cleanPath
        $startInfo.EnvironmentVariables['QT_PLUGIN_PATH'] = Join-Path $stagedRoot 'plugins'
        $startInfo.EnvironmentVariables['QT_QPA_PLATFORM_PLUGIN_PATH'] = Join-Path $stagedRoot 'plugins\platforms'
        $startInfo.EnvironmentVariables['GST_PLUGIN_PATH_1_0'] = Join-Path $stagedRoot 'lib\gstreamer-1.0'
        $startInfo.EnvironmentVariables['GST_PLUGIN_SYSTEM_PATH_1_0'] = Join-Path $stagedRoot 'lib\gstreamer-1.0'
        $startInfo.EnvironmentVariables['GST_PLUGIN_SCANNER_1_0'] = Join-Path $stagedRoot 'libexec\gstreamer-1.0\gst-plugin-scanner.exe'
        $startInfo.EnvironmentVariables['GST_REGISTRY_1_0'] = Join-Path $runDirectory 'gst-registry.bin'

        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $startInfo
        if (-not $process.Start()) {
            throw 'Staged application simple boot test could not be started.'
        }
        if (-not $process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            $stderr = $process.StandardError.ReadToEnd().Trim()
            throw "Staged application simple boot test timed out after 30 seconds. $stderr"
        }
        $process.WaitForExit()

        $stdout = $process.StandardOutput.ReadToEnd().Trim()
        $stderr = $process.StandardError.ReadToEnd().Trim()
        if (-not [string]::IsNullOrWhiteSpace($stdout)) {
            Write-Host $stdout
        }
        if (-not [string]::IsNullOrWhiteSpace($stderr)) {
            Write-Host $stderr
        }
        if ($process.ExitCode -ne 0) {
            throw "Staged application simple boot test failed with exit code $($process.ExitCode)."
        }
        Write-Host 'Staged application simple boot test passed.'
    }
    catch {
        throw ('Staged application simple boot test failed at line {0}: {1}' -f $_.InvocationInfo.ScriptLineNumber, $_.Exception.Message)
    }
    finally {
        if ($null -ne $process) {
            $process.Dispose()
        }
    }
}

function Replace-Required {
    param(
        [Parameter(Mandatory = $true)][string]$Content,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Replacement,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $regex = New-Object System.Text.RegularExpressions.Regex($Pattern)
    if (-not $regex.IsMatch($Content)) {
        throw "Version pattern not found: $Label"
    }
    return $regex.Replace($Content, $Replacement)
}

function Set-ReleaseVersion {
    $overridesPath = Join-Path $projectRoot 'custom\cmake\CustomOverrides.cmake'
    $resourcePath = Join-Path $projectRoot 'custom\GRobot.rc'
    $commaVersion = ($Version -replace '\.', ',') + ',0'

    $content = Get-Content -LiteralPath $overridesPath -Raw
    $content = Replace-Required $content 'set\(QGC_APP_VERSION_OVERRIDE "\d+\.\d+\.\d+"\)' ("set(QGC_APP_VERSION_OVERRIDE `"{0}`")" -f $Version) 'QGC_APP_VERSION_OVERRIDE'
    $content = Replace-Required $content 'set\(QGC_APP_VERSION_STR_OVERRIDE "\d+\.\d+\.\d+"\)' ("set(QGC_APP_VERSION_STR_OVERRIDE `"{0}`")" -f $Version) 'QGC_APP_VERSION_STR_OVERRIDE'
    if ($content -ne [System.IO.File]::ReadAllText($overridesPath)) {
        Write-Utf8NoBom -LiteralPath $overridesPath -Content $content
    }

    $content = Get-Content -LiteralPath $resourcePath -Raw
    $content = Replace-Required $content '(?m)^ FILEVERSION \d+,\d+,\d+,\d+$' (" FILEVERSION {0}" -f $commaVersion) 'Windows FILEVERSION'
    $content = Replace-Required $content '(?m)^ PRODUCTVERSION \d+,\d+,\d+,\d+$' (" PRODUCTVERSION {0}" -f $commaVersion) 'Windows PRODUCTVERSION'
    $content = Replace-Required $content 'VALUE "FileVersion", "\d+\.\d+\.\d+"' ("VALUE `"FileVersion`", `"{0}`"" -f $Version) 'Windows FileVersion string'
    $content = Replace-Required $content 'VALUE "ProductVersion", "\d+\.\d+\.\d+"' ("VALUE `"ProductVersion`", `"{0}`"" -f $Version) 'Windows ProductVersion string'
    if ($content -ne [System.IO.File]::ReadAllText($resourcePath)) {
        Write-Utf8NoBom -LiteralPath $resourcePath -Content $content
    }

    Write-Host "Version sources updated to $Version"
}

function Get-NormalizedProductVersion {
    param([Parameter(Mandatory = $true)][string]$ProductVersion)
    if ($ProductVersion -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
        throw "Unexpected product version format: $ProductVersion"
    }
    if (($ProductVersion -split '\.').Count -eq 3) {
        return "$ProductVersion.0"
    }
    return $ProductVersion
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)

    $stream = [System.IO.File]::OpenRead($LiteralPath)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Assert-UpgradeContract {
    $nsisScript = Get-Content -LiteralPath (Join-Path $projectRoot 'deploy\windows\nullsoft_installer.nsi') -Raw
    if ($nsisScript -notmatch 'InstallDir "\$PROGRAMFILES64\\\$\{APPNAME\}"') {
        throw 'Stable QGC_KevinJiang install directory contract is missing.'
    }
    if ($nsisScript -notmatch 'ExecWait "\$R0 /S -LEAVE_DATA=1') {
        throw 'In-place upgrade and user-data preservation contract is missing.'
    }
    if ($nsisScript -notmatch '(?m)ExecWait "\$R0 /S -LEAVE_DATA=1[^"\r\n]*" \$0') {
        throw 'Previous uninstaller return-code capture is missing.'
    }
}

function Assert-StagedApplication {
    param([switch]$RunBootTest)

    $applicationPath = Join-Path $stagingDirectory 'bin\QGC_KevinJiang.exe'
    $requiredRuntimeFiles = @(
        'plugins\platforms\qwindows.dll',
        'bin\Qt6Core.dll',
        'bin\Qt6Gui.dll',
        'bin\Qt6Qml.dll',
        'bin\Qt6Quick.dll',
        'bin\gstgl-1.0-0.dll',
        'bin\gstreamer-1.0-0.dll',
        'bin\ai_detection\run_yolo_to_qgc_auto.py',
        'bin\ai_detection\ai_detection_core.py',
        'bin\ai_detection\yolo_to_qgc_udp.py'
    )
    foreach ($relativePath in $requiredRuntimeFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $stagingDirectory $relativePath) -PathType Leaf)) {
            throw "Required runtime file is missing: $relativePath"
        }
        Write-Host "Runtime OK: $relativePath"
    }
    $application = Get-Item -LiteralPath $applicationPath
    if ((Get-NormalizedProductVersion $application.VersionInfo.ProductVersion) -ne "$Version.0") {
        throw "Application product version mismatch: $($application.VersionInfo.ProductVersion)"
    }
    if ($RunBootTest) {
        Invoke-StagedBootTest -ApplicationPath $applicationPath
    }
    return $requiredRuntimeFiles
}

function Get-ReleaseCandidate {
    return [ordered]@{
        installerSha256 = Get-FileSha256 -LiteralPath $installerPath
        applicationSha256 = Get-FileSha256 -LiteralPath (Join-Path $stagingDirectory 'bin\QGC_KevinJiang.exe')
        installerScriptSha256 = Get-FileSha256 -LiteralPath (Join-Path $projectRoot 'deploy\windows\nullsoft_installer.nsi')
    }
}

function Restore-VerificationInputs {
    $auditRoot = [System.IO.Path]::GetFullPath((Join-Path $distributionDirectory 'release-audit')) + '\'
    $sourceDirectory = [System.IO.Path]::GetFullPath($AuditDirectory)
    if (-not ($sourceDirectory + '\').StartsWith($auditRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Verification must use an audit directory for this project and requested version.'
    }
    $sourcePath = Join-Path $sourceDirectory 'release-state.json'
    $source = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
    if ($source.schemaVersion -ne 2 -or $source.mode -ne 'release' -or
        $source.requestedVersion -ne $Version -or $source.projectRoot -ne $projectRoot -or
        $source.buildDirectory -ne $buildDirectory -or $source.installerPath -ne $installerPath) {
        throw 'Verification requires a matching release audit from the updated workflow.'
    }
    if ($source.status -notin @('failed', 'succeeded') -or $source.steps.Count -ne 8 -or
        $source.steps[-1].name -ne 'Verify release artifact' -or
        $source.steps[-1].status -notin @('failed', 'succeeded') -or
        @($source.steps | Select-Object -First 7 | Where-Object { $_.status -ne 'succeeded' }).Count -ne 0 -or
        $null -eq $source.candidate) {
        throw 'The source release must have completed staging, boot verification and installer creation.'
    }
    $stagingRoot = [System.IO.Path]::GetFullPath((Join-Path $buildDirectory 'release-runs')) + '\'
    $sourceStaging = [System.IO.Path]::GetFullPath($source.stagingDirectory)
    if (-not ($sourceStaging + '\').StartsWith($stagingRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Source staging directory is outside the release build.'
    }
    $script:stagingDirectory = $sourceStaging
    $script:Candidate = $source.candidate
    $script:SourceAudit = $sourcePath
    $script:NsisPath = $source.nsisPath
    $script:Jobs = $source.compilerJobs
}

function Confirm-ReleaseArtifact {
    $currentCandidate = Get-ReleaseCandidate
    foreach ($field in @('installerSha256', 'applicationSha256', 'installerScriptSha256')) {
        if ($currentCandidate[$field] -ne $script:Candidate.$field) {
            throw "Release input changed since installer creation: $field. Rebuild the release."
        }
    }
    $requiredRuntimeFiles = @(Assert-StagedApplication -RunBootTest:$VerifyOnly)
    Assert-UpgradeContract
    $installer = Get-Item -LiteralPath $installerPath
    $application = Get-Item -LiteralPath (Join-Path $stagingDirectory 'bin\QGC_KevinJiang.exe')
    if ((Get-NormalizedProductVersion $installer.VersionInfo.ProductVersion) -ne "$Version.0") {
        throw "Installer product version mismatch: $($installer.VersionInfo.ProductVersion)"
    }
    $securityModule = Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1'
    Import-Module -Name $securityModule -ErrorAction Stop
    $signature = (Get-AuthenticodeSignature -LiteralPath $installerPath).Status.ToString()
    $script:Artifact = [ordered]@{
        installerPath = $installer.FullName
        installerVersion = $installer.VersionInfo.ProductVersion
        applicationVersion = $application.VersionInfo.ProductVersion
        sizeBytes = $installer.Length
        sha256 = $currentCandidate.installerSha256
        signature = $signature
        upgradeIdentity = 'QGC_KevinJiang'
        preservesUserData = $true
        requiredRuntimeFilesVerified = $requiredRuntimeFiles
    }
    Write-Host "SHA-256: $($script:Artifact.sha256)"
    Write-Host "Signature: $signature"
}

function Write-FinalReport {
    $stepRows = foreach ($step in $script:Steps) {
        "| {0} | {1} | {2} | {3} |" -f $step.number, $step.name, $step.status, $step.durationSeconds
    }
    $artifactLines = if ($null -ne $script:Artifact) {
        @(
            "- Installer: $($script:Artifact.installerPath)"
            "- Installer version: $($script:Artifact.installerVersion)"
            "- Application version: $($script:Artifact.applicationVersion)"
            "- SHA-256: $($script:Artifact.sha256)"
            "- Signature: $($script:Artifact.signature)"
            "- Size: $($script:Artifact.sizeBytes) bytes"
        )
    }
    else {
        @('- No distributable artifact was produced.')
    }

    $markdown = @(
        "# QGC_KevinJiang Windows release report"
        ""
        "- Run: $runId"
        "- Mode: $($script:Mode)"
        "- Compiler jobs: $Jobs"
        "- Source audit: $($script:SourceAudit)"
        "- Requested version: $Version"
        "- Status: $($script:RunStatus)"
        "- Failure: $($script:FailureMessage)"
        ""
        "## Steps"
        ""
        "| # | Step | Status | Seconds |"
        "|---:|---|---|---:|"
        $stepRows
        ""
        "## Artifact"
        ""
        $artifactLines
        ""
        "Detailed logs are stored in steps/."
    ) -join [Environment]::NewLine

    Write-Utf8NoBom -LiteralPath $reportPath -Content $markdown
}

$script:StartedAt = (Get-Date).ToString('o')
Write-RunState

try {
    if ($VerifyOnly) {
        Invoke-ReleaseStep -Name 'Verify release artifact' -Action {
            Restore-VerificationInputs
            Confirm-ReleaseArtifact
        }
        $script:RunStatus = 'succeeded'
        Write-Host "Verification completed. Report: $reportPath"
        return
    }

    Invoke-ReleaseStep -Name 'Preflight' -Action {
        $requiredFiles = @(
            'CMakeLists.txt',
            'custom\cmake\CustomOverrides.cmake',
            'custom\GRobot.rc',
            'src\AppSettings\HelpSettings.qml',
            'deploy\windows\nullsoft_installer.nsi',
            'deploy\windows\installheader.bmp',
            'branding\GRobot_icons\GRobot_taskbar.ico',
            'tools\ai_detection\run_yolo_to_qgc_auto.py',
            'tools\ai_detection\ai_detection_core.py',
            'tools\ai_detection\yolo_to_qgc_udp.py'
        )
        foreach ($relativePath in $requiredFiles) {
            $path = Join-Path $projectRoot $relativePath
            if (-not (Test-Path -LiteralPath $path)) {
                throw "Required release input is missing: $path"
            }
            Write-Host "OK: $path"
        }

        $helpContent = Get-Content -LiteralPath (Join-Path $projectRoot 'src\AppSettings\HelpSettings.qml') -Raw
        if ($helpContent -notmatch '\.arg\(QGroundControl\.qgcVersion\)') {
            throw 'Help page must display the runtime QGroundControl.qgcVersion instead of a fixed release number.'
        }
        Write-Host 'OK: Help page version is bound to QGroundControl.qgcVersion'
        Assert-UpgradeContract
        $securityModule = Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1'
        Import-Module -Name $securityModule -ErrorAction Stop

        if (-not (Test-Path -LiteralPath 'D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat')) {
            throw 'Visual Studio build environment is missing.'
        }
        if ($null -eq (Get-Command cmake -ErrorAction SilentlyContinue)) {
            throw 'cmake is not available on PATH.'
        }
        foreach ($dependency in @((Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'),
                                  (Join-Path $gstreamerRoot 'bin\gst-inspect-1.0.exe'),
                                  $pythonExecutable)) {
            if (-not (Test-Path -LiteralPath $dependency -PathType Leaf)) {
                throw "Required v5.1.5 build dependency is missing: $dependency"
            }
            Write-Host "Dependency OK: $dependency"
        }

        $nsisCandidates = @(
            (Join-Path $buildDirectory 'nsis-portable\nsis-3.11\Bin\makensis.exe'),
            (Join-Path $buildDirectory 'nsis-portable\nsis-3.11\makensis.exe'),
            (Join-Path $projectRoot 'build-release\nsis-portable\nsis-3.11\Bin\makensis.exe'),
            (Join-Path $projectRoot 'build-release\nsis-portable\nsis-3.11\makensis.exe'),
            'C:\Program Files\NSIS\makensis.exe',
            'C:\Program Files (x86)\NSIS\makensis.exe'
        )
        $script:NsisPath = $nsisCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if ([string]::IsNullOrWhiteSpace($script:NsisPath)) {
            throw "NSIS was not found. Expected portable NSIS under $buildDirectory\nsis-portable."
        }
        $script:NsisPath = (Resolve-Path -LiteralPath $script:NsisPath).Path
        Write-Host "NSIS: $script:NsisPath"

        if (-not $PreflightOnly -and (Test-Path -LiteralPath $installerPath) -and -not $Force) {
            throw "Installer already exists: $installerPath. Use -Force only for an intentional rebuild."
        }
    }

    if ($PreflightOnly) {
        $script:RunStatus = 'preflight-succeeded'
        Write-Host "Preflight completed. Report: $reportPath"
        return
    }

    Invoke-ReleaseStep -Name 'Synchronize version sources' -Action {
        Set-ReleaseVersion
    }

    Invoke-ReleaseStep -Name 'Configure Release' -Action {
        $previousPath = $env:PATH
        try {
            $env:PATH = "{0};{1};{2};{3}" -f (Join-Path $qtRoot 'bin'), (Join-Path $gstreamerRoot 'bin'),
                                               (Split-Path -Parent $pythonExecutable), $previousPath
            $command = 'cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DQGC_DEBUG_CANDIDATE=OFF -DQGC_BUILD_TESTING=OFF -DQGC_BUILD_INSTALLER=ON -DQGC_USE_CACHE=OFF -DCPM_SOURCE_CACHE=D:/Develop/envs/qgc-cpm-cache "-DCMAKE_PREFIX_PATH={2}" "-DGStreamer_ROOT_DIR={3}" "-DPython3_EXECUTABLE={4}" "-DPython_EXECUTABLE={4}" "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /nologo" "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /nologo"' -f $projectRoot, $buildDirectory, $qtRoot, $gstreamerRoot, $pythonExecutable
            Invoke-VsCommand -Command $command
            # CMake 4.1 can decode the Chinese MSVC /showIncludes prefix incorrectly.
            # Keep Ninja header dependency tracking consistent with the compiler output.
            $prefixCorrected = $false
            $prefixPattern = '(?m)^set\(CMAKE_(C|CXX)_CL_SHOWINCLUDES_PREFIX "娉ㄦ剰: 鍖呭惈鏂囦欢: *"\)(?=\r?$)'
            foreach ($compilerFile in Get-ChildItem -LiteralPath (Join-Path $buildDirectory 'CMakeFiles') -Filter 'CMake*Compiler.cmake' -Recurse) {
                $compilerContent = Get-Content -LiteralPath $compilerFile.FullName -Raw
                if ($compilerContent -match $prefixPattern) {
                    $compilerContent = $compilerContent -replace $prefixPattern, 'set(CMAKE_$1_CL_SHOWINCLUDES_PREFIX "注意: 包含文件: ")'
                    Write-Utf8NoBom -LiteralPath $compilerFile.FullName -Content $compilerContent
                    $prefixCorrected = $true
                    Write-Host "Corrected MSVC include prefix: $($compilerFile.FullName)"
                }
            }
            if ($prefixCorrected) {
                $dependencyLog = Join-Path $buildDirectory '.ninja_deps'
                if (Test-Path -LiteralPath $dependencyLog) {
                    Move-Item -LiteralPath $dependencyLog -Destination "$dependencyLog.before-prefix-repair-$runId"
                }
                Invoke-VsCommand -Command $command
            }
        }
        finally {
            $env:PATH = $previousPath
        }
    }

    Invoke-ReleaseStep -Name 'Build Release' -Action {
        $command = 'cmake --build "{0}" --config Release --parallel {1}' -f $buildDirectory, $Jobs
        Invoke-VsCommand -Command $command
    }

    Invoke-ReleaseStep -Name 'Stage dependencies' -Action {
        # Run all deployment rules, deferring only NSIS until the staged app passes.
        $command = 'cmake "-DCMAKE_INSTALL_PREFIX={0}" -DCMAKE_INSTALL_CONFIG_NAME=Release -DQGC_SKIP_WINDOWS_INSTALLER=ON -P "{1}\cmake_install.cmake"' -f $stagingDirectory, $buildDirectory
        Invoke-VsCommand -Command $command
    }

    Invoke-ReleaseStep -Name 'Verify staged application' -Action {
        Assert-StagedApplication -RunBootTest | Out-Null
    }

    Invoke-ReleaseStep -Name 'Build installer' -Action {
        $previousPath = $env:PATH
        try {
            $env:PATH = "{0};{1}" -f (Split-Path -Parent $script:NsisPath), $previousPath
            $command = 'cmake --install "{0}" --config Release --prefix "{1}" --component windows-installer' -f $buildDirectory, $stagingDirectory
            Invoke-VsCommand -Command $command
        }
        finally {
            $env:PATH = $previousPath
        }

        $installScript = Get-Content -LiteralPath (Join-Path $buildDirectory 'cmake_install.cmake') -Raw
        $installerMatches = [regex]::Matches($installScript, 'set\(QGC_WINDOWS_OUT "([^"\r\n]+)"\)')
        if ($installerMatches.Count -ne 1) {
            throw 'Expected one architecture-specific QGC_WINDOWS_OUT in the generated install script.'
        }
        $generatedInstaller = $installerMatches[0].Groups[1].Value
        if (-not (Test-Path -LiteralPath $generatedInstaller)) {
            throw "Expected installer was not generated: $generatedInstaller"
        }
        Copy-Item -LiteralPath $generatedInstaller -Destination $installerPath -Force:$Force
        $script:Candidate = Get-ReleaseCandidate
        Write-Host "Installer copied to $installerPath"
    }

    Invoke-ReleaseStep -Name 'Verify release artifact' -Action {
        Confirm-ReleaseArtifact
    }

    $script:RunStatus = 'succeeded'
}
catch {
    $script:RunStatus = 'failed'
    $script:FailureMessage = '{0} (line {1})' -f $_.Exception.Message, $_.InvocationInfo.ScriptLineNumber
}
finally {
    Write-RunState
    Write-FinalReport
}

if ($script:RunStatus -ne 'succeeded') {
    Write-Error "Release failed. Audit report: $reportPath"
    exit 1
}

Write-Host "Release succeeded: $installerPath"
Write-Host "Audit report: $reportPath"
