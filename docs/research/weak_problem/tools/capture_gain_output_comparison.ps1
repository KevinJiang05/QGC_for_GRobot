$ErrorActionPreference = "Stop"
$captureStarted = $false

function Wait-Step([string]$message) {
    [void](Read-Host $message)
}

try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "没有管理员权限。请右键 CMD 文件并选择‘以管理员身份运行’。"
    }

    $outputDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\logs"))
    [void](New-Item -ItemType Directory -Path $outputDirectory -Force)
    $tag = Get-Date -Format "yyyyMMdd_HHmmss"
    $etlPath = Join-Path $outputDirectory "gain_output_$tag.etl"
    $pcapPath = Join-Path $outputDirectory "gain_output_$tag.pcapng"

    Clear-Host
    Write-Host "============================================================"
    Write-Host "PilotGain 50 / 75 / 100 电机输出同步抓包"
    Write-Host "============================================================"
    Write-Host ""
    Write-Host "前提：机器可靠固定、推进器周围无人、油门已回中。" -ForegroundColor Yellow
    Write-Host "出现异常振动、异响、过流、失联或不可预测输出时：立即回中并解除锁定。"
    Write-Host "本脚本只抓包，不会控制、解锁或改变飞控参数。"
    Write-Host ""
    Wait-Step "确认安全条件后按回车开始抓包"

    & pktmon filter add "Gain_Output_$tag" -t TCP -i 192.168.1.200 -p 4019
    if ($LASTEXITCODE -ne 0) { throw "添加抓包过滤器失败：$LASTEXITCODE" }
    & pktmon start --capture --pkt-size 0 --file-name $etlPath
    if ($LASTEXITCODE -ne 0) { throw "启动抓包失败：$LASTEXITCODE" }
    $captureStarted = $true

    Write-Host ""
    Write-Host "步骤 1 / 4：把 PilotGain 调到 50%，确认油门回中，然后解锁。" -ForegroundColor Cyan
    Wait-Step "完成后按回车"
    Write-Host "将油门圆点推到右半段约 50% 的固定位置，保持 2 秒，然后回中。"
    Wait-Step "推杆并回中后按回车"

    Write-Host ""
    Write-Host "步骤 2 / 4：把 PilotGain 调到 75%，不要改变其他设置。" -ForegroundColor Cyan
    Wait-Step "完成后按回车"
    Write-Host "推到与上次相同的位置，保持 2 秒，然后回中。"
    Wait-Step "推杆并回中后按回车"

    Write-Host ""
    Write-Host "步骤 3 / 4：把 PilotGain 调到 100%，不要改变其他设置。" -ForegroundColor Cyan
    Wait-Step "完成后按回车"
    Write-Host "推到与前两次相同的位置，保持 2 秒，然后回中。"
    Wait-Step "推杆并回中后按回车"

    Write-Host ""
    Write-Host "步骤 4 / 4：保持油门回中，立即解除锁定。" -ForegroundColor Yellow
    Wait-Step "确认已经解除锁定后按回车停止抓包"

    & pktmon stop
    $captureStarted = $false
    if ($LASTEXITCODE -ne 0) { throw "停止抓包失败：$LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $etlPath)) { throw "没有生成 ETL 文件。" }

    & pktmon etl2pcap $etlPath -o $pcapPath
    if ($LASTEXITCODE -ne 0) { throw "转换 PCAPNG 失败：$LASTEXITCODE" }
    if (-not (Test-Path -LiteralPath $pcapPath)) { throw "没有生成 PCAPNG 文件。" }

    Write-Host ""
    Write-Host "抓包完成：$pcapPath" -ForegroundColor Green
    Wait-Step "按回车关闭，然后告诉 Codex‘增益对比抓包完成’"
    exit 0
}
catch {
    if ($captureStarted) {
        & pktmon stop | Out-Null
    }
    Write-Host ""
    Write-Host "测试脚本失败：$($_.Exception.Message)" -ForegroundColor Red
    Write-Host "请确认油门回中并解除锁定。"
    Wait-Step "按回车关闭窗口"
    exit 2
}
