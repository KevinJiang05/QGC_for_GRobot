[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,

    [switch]$PreflightOnly,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildDirectory = Join-Path $projectRoot 'build-release'
$distributionDirectory = Join-Path $projectRoot ("dist\QGC_KevinJiang_v{0}" -f $Version)
$installerName = "QGC_v{0}_KevinJiang-installer.exe" -f $Version
$installerPath = Join-Path $distributionDirectory $installerName
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDirectory = Join-Path $distributionDirectory ("release-audit\{0}" -f $runId)
$logDirectory = Join-Path $runDirectory 'steps'
$stagingDirectory = Join-Path $buildDirectory ("release-runs\v{0}\{1}\staging" -f $Version, $runId)
$statePath = Join-Path $runDirectory 'release-state.json'
$reportPath = Join-Path $runDirectory 'release-report.md'

New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null

$script:RunStatus = 'running'
$script:FailureMessage = $null
$script:StepNumber = 0
$script:Steps = [System.Collections.Generic.List[object]]::new()
$script:Artifact = $null
$script:NsisPath = $null

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
        schemaVersion = 1
        application = 'QGC_KevinJiang'
        requestedVersion = $Version
        runId = $runId
        mode = $(if ($PreflightOnly) { 'preflight' } else { 'release' })
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
        Write-Host ("[{0}/{1}] {2}" -f $script:StepNumber, 6, $Name)
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
    $helpPath = Join-Path $projectRoot 'src\UI\AppSettings\HelpSettings.qml'
    $commaVersion = ($Version -replace '\.', ',') + ',0'

    $content = Get-Content -LiteralPath $overridesPath -Raw
    $content = Replace-Required $content 'set\(QGC_APP_VERSION_OVERRIDE "\d+\.\d+\.\d+"\)' ("set(QGC_APP_VERSION_OVERRIDE `"{0}`")" -f $Version) 'QGC_APP_VERSION_OVERRIDE'
    $content = Replace-Required $content 'set\(QGC_APP_VERSION_STR_OVERRIDE "\d+\.\d+\.\d+"\)' ("set(QGC_APP_VERSION_STR_OVERRIDE `"{0}`")" -f $Version) 'QGC_APP_VERSION_STR_OVERRIDE'
    Write-Utf8NoBom -LiteralPath $overridesPath -Content $content

    $content = Get-Content -LiteralPath $resourcePath -Raw
    $content = Replace-Required $content '(?m)^ FILEVERSION \d+,\d+,\d+,\d+$' (" FILEVERSION {0}" -f $commaVersion) 'Windows FILEVERSION'
    $content = Replace-Required $content '(?m)^ PRODUCTVERSION \d+,\d+,\d+,\d+$' (" PRODUCTVERSION {0}" -f $commaVersion) 'Windows PRODUCTVERSION'
    $content = Replace-Required $content 'VALUE "FileVersion", "\d+\.\d+\.\d+"' ("VALUE `"FileVersion`", `"{0}`"" -f $Version) 'Windows FileVersion string'
    $content = Replace-Required $content 'VALUE "ProductVersion", "\d+\.\d+\.\d+"' ("VALUE `"ProductVersion`", `"{0}`"" -f $Version) 'Windows ProductVersion string'
    Write-Utf8NoBom -LiteralPath $resourcePath -Content $content

    $content = Get-Content -LiteralPath $helpPath -Raw
    $content = Replace-Required $content 'QGC_KevinJiang \d+\.\d+\.\d+ 基于开源' ("QGC_KevinJiang {0} 基于开源" -f $Version) 'Help page version'
    Write-Utf8NoBom -LiteralPath $helpPath -Content $content

    Write-Host "Version sources updated to $Version"
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
        "- Mode: $(if ($PreflightOnly) { 'preflight' } else { 'release' })"
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
    Invoke-ReleaseStep -Name 'Preflight' -Action {
        $requiredFiles = @(
            'CMakeLists.txt',
            'custom\cmake\CustomOverrides.cmake',
            'custom\GRobot.rc',
            'src\UI\AppSettings\HelpSettings.qml',
            'deploy\windows\nullsoft_installer.nsi',
            'deploy\windows\driver.msi',
            'deploy\windows\installheader.bmp',
            'branding\GRobot_icons\GRobot_taskbar.ico'
        )
        foreach ($relativePath in $requiredFiles) {
            $path = Join-Path $projectRoot $relativePath
            if (-not (Test-Path -LiteralPath $path)) {
                throw "Required release input is missing: $path"
            }
            Write-Host "OK: $path"
        }

        if (-not (Test-Path -LiteralPath 'D:\Develop\Toolchains\VS2022BuildTools\Common7\Tools\VsDevCmd.bat')) {
            throw 'Visual Studio build environment is missing.'
        }
        if ($null -eq (Get-Command cmake -ErrorAction SilentlyContinue)) {
            throw 'cmake is not available on PATH.'
        }

        $nsisCandidates = @(
            (Join-Path $buildDirectory 'nsis-portable\nsis-3.11\Bin\makensis.exe'),
            (Join-Path $buildDirectory 'nsis-portable\nsis-3.11\makensis.exe'),
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
        Write-RunState
        Write-FinalReport
        Write-Host "Preflight completed. Report: $reportPath"
        return
    }

    Invoke-ReleaseStep -Name 'Synchronize version sources' -Action {
        Set-ReleaseVersion
    }

    Invoke-ReleaseStep -Name 'Configure Release' -Action {
        $command = 'cmake -S "{0}" -B "{1}"' -f $projectRoot, $buildDirectory
        Invoke-VsCommand -Command $command
    }

    Invoke-ReleaseStep -Name 'Build Release' -Action {
        $command = 'cmake --build "{0}" --config Release --parallel 1' -f $buildDirectory
        Invoke-VsCommand -Command $command
    }

    Invoke-ReleaseStep -Name 'Stage dependencies and build installer' -Action {
        $previousPath = $env:PATH
        try {
            $env:PATH = "{0};{1}" -f (Split-Path -Parent $script:NsisPath), $previousPath
            $command = 'cmake --install "{0}" --config Release --prefix "{1}"' -f $buildDirectory, $stagingDirectory
            Invoke-VsCommand -Command $command
        }
        finally {
            $env:PATH = $previousPath
        }

        $generatedInstaller = Join-Path $buildDirectory 'QGC_KevinJiang-installer.exe'
        if (-not (Test-Path -LiteralPath $generatedInstaller)) {
            throw "Expected installer was not generated: $generatedInstaller"
        }
        Copy-Item -LiteralPath $generatedInstaller -Destination $installerPath -Force:$Force
        Write-Host "Installer copied to $installerPath"
    }

    Invoke-ReleaseStep -Name 'Verify release artifact' -Action {
        $applicationPath = Join-Path $stagingDirectory 'bin\QGC_KevinJiang.exe'
        $requiredRuntimeFiles = @(
            'plugins\platforms\qwindows.dll',
            'bin\Qt6Core.dll',
            'bin\Qt6Gui.dll',
            'bin\Qt6Qml.dll',
            'bin\Qt6Quick.dll',
            'bin\gstreamer-1.0-0.dll'
        )
        foreach ($relativePath in $requiredRuntimeFiles) {
            $path = Join-Path $stagingDirectory $relativePath
            if (-not (Test-Path -LiteralPath $path)) {
                throw "Required runtime file is missing: $path"
            }
            Write-Host "Runtime OK: $relativePath"
        }

        $installer = Get-Item -LiteralPath $installerPath
        $application = Get-Item -LiteralPath $applicationPath
        if ($installer.VersionInfo.ProductVersion -ne $Version) {
            throw "Installer product version mismatch: $($installer.VersionInfo.ProductVersion)"
        }
        if ($application.VersionInfo.ProductVersion -ne $Version) {
            throw "Application product version mismatch: $($application.VersionInfo.ProductVersion)"
        }

        $nsisScript = Get-Content -LiteralPath (Join-Path $projectRoot 'deploy\windows\nullsoft_installer.nsi') -Raw
        if ($nsisScript -notmatch 'InstallDir "\$PROGRAMFILES64\\\$\{APPNAME\}"') {
            throw 'Stable QGC_KevinJiang install directory contract is missing.'
        }
        if ($nsisScript -notmatch 'ExecWait "\$R0 /S -LEAVE_DATA=1') {
            throw 'In-place upgrade and user-data preservation contract is missing.'
        }

        $hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash
        $signature = (Get-AuthenticodeSignature -LiteralPath $installerPath).Status.ToString()
        $script:Artifact = [ordered]@{
            installerPath = $installer.FullName
            installerVersion = $installer.VersionInfo.ProductVersion
            applicationVersion = $application.VersionInfo.ProductVersion
            sizeBytes = $installer.Length
            sha256 = $hash
            signature = $signature
            upgradeIdentity = 'QGC_KevinJiang'
            preservesUserData = $true
            requiredRuntimeFilesVerified = $requiredRuntimeFiles
        }

        Write-Host "SHA-256: $hash"
        Write-Host "Signature: $signature"
    }

    $script:RunStatus = 'succeeded'
}
catch {
    $script:RunStatus = 'failed'
    $script:FailureMessage = $_.Exception.Message
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
