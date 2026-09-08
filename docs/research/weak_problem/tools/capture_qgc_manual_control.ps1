$ErrorActionPreference = "Stop"

function Wait-ForClose {
    param([string]$Message = "按回车键关闭窗口")
    [void](Read-Host $Message)
}

try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

    if (-not $isAdmin) {
        Write-Host ""
        Write-Host "错误：当前没有管理员权限。" -ForegroundColor Red
        Write-Host "请关闭窗口，右键 capture_qgc_manual_control.cmd，选择‘以管理员身份运行’。"
        Wait-ForClose
        exit 1
    }

    $outputDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\logs"))
    [void](New-Item -ItemType Directory -Path $outputDirectory -Force)
    $tag = Get-Date -Format "yyyyMMdd_HHmmss"
    $etlPath = Join-Path $outputDirectory "qgc_mavlink_$tag.etl"
    $pcapPath = Join-Path $outputDirectory "qgc_mavlink_$tag.pcapng"
    $filterName = "QGC_MAVLink_$tag"

    Clear-Host
    Write-Host "============================================================"
    Write-Host "QGC / MAVLink 手柄控制被动抓包"
    Write-Host "============================================================"
    Write-Host ""
    Write-Host "安全要求：" -ForegroundColor Yellow
    Write-Host "  1. 机器保持机械固定，推进器周围无人。"
    Write-Host "  2. 飞控必须保持未解锁。"
    Write-Host "  3. 不按 Xbox 的 A、B、菜单、肩键或摇杆按下键。"
    Write-Host "  4. 本程序只抓取网络数据，不发送任何 MAVLink 命令。"
    Write-Host ""
    Write-Host "抓包目标：QGC 到 192.168.1.200:4019 的 TCP 流量。"
    Write-Host "输出文件：$pcapPath"
    Write-Host ""
    [void](Read-Host "确认以上安全条件后，按回车键开始抓包")

    Write-Host "正在添加抓包过滤器..."
    & pktmon filter add $filterName -t TCP -i 192.168.1.200 -p 4019
    if ($LASTEXITCODE -ne 0) {
        throw "添加 PktMon 过滤器失败，退出码：$LASTEXITCODE"
    }

    Write-Host "正在启动抓包..."
    & pktmon start --capture --pkt-size 0 --file-name $etlPath
    if ($LASTEXITCODE -ne 0) {
        throw "启动 PktMon 抓包失败，退出码：$LASTEXITCODE"
    }

    Write-Host ""
    Write-Host "抓包已经开始，请现在操作 Xbox 油门轴：" -ForegroundColor Green
    Write-Host "  1. 松开油门，让 QGC 油门圆点回到中间。"
    Write-Host "  2. 推到约 25%，停留 1 秒，然后回中。"
    Write-Host "  3. 推到约 50%，停留 1 秒，然后回中。"
    Write-Host "  4. 推到 0%，停留 1 秒，然后回中。"
    Write-Host "  5. 不要解锁，不要按任何手柄按钮。"
    Write-Host ""
    [void](Read-Host "全部操作完成后，按回车键停止抓包")

    Write-Host "正在停止抓包..."
    & pktmon stop
    if ($LASTEXITCODE -ne 0) {
        throw "停止 PktMon 抓包失败，退出码：$LASTEXITCODE"
    }

    if (-not (Test-Path -LiteralPath $etlPath)) {
        throw "抓包已停止，但没有生成 ETL 文件：$etlPath"
    }

    Write-Host "正在转换为 PCAPNG..."
    & pktmon etl2pcap $etlPath -o $pcapPath
    if ($LASTEXITCODE -ne 0) {
        throw "ETL 转换失败，退出码：$LASTEXITCODE；原始文件仍保留在 $etlPath"
    }

    if (-not (Test-Path -LiteralPath $pcapPath)) {
        throw "转换程序报告成功，但没有生成 PCAPNG 文件。"
    }

    Write-Host ""
    Write-Host "抓包完成。" -ForegroundColor Green
    Write-Host "PCAPNG：$pcapPath"
    Write-Host "ETL：$etlPath"
    Write-Host ""
    Wait-ForClose "按回车键关闭窗口，然后告诉 Codex‘抓包完成’"
    exit 0
}
catch {
    Write-Host ""
    Write-Host "抓包失败：$($_.Exception.Message)" -ForegroundColor Red
    Write-Host "窗口会保持打开，便于查看错误。"
    Wait-ForClose
    exit 2
}
