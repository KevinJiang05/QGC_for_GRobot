/****************************************************************************
 *
 * DeepShark status and event panel.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property bool minimized: false
    property real panelWidth: Math.min(Math.max(ScreenTools.defaultFontPixelWidth * 38, 320),
                                       Math.max(ScreenTools.defaultFontPixelWidth * 24,
                                                parent ? parent.width - ScreenTools.defaultFontPixelWidth * 2 : 320))
    property string layoutMode: "grid"
    property int mainIndex: 0
    property string mainName: ""
    property bool mapHidden: false
    property bool deepSharkPanelMinimized: false
    property string vehicleConnectionStatus: qsTr("未连接")
    property int vehicleConnectionLevel: 0
    property string vehicleFlightMode: qsTr("暂无数据")
    property string vehicleArmStatus: qsTr("暂无数据")
    property string vehicleBatteryStatus: qsTr("暂无数据")
    property bool aiRunning: false
    property bool aiReceiverBound: false
    property int aiDetectionCount: 0
    property string aiDetail: ""
    property var videoRows: []
    property var rtspRows: videoRows
    property var recentEvents: []
    property bool diagnosticsExpanded: false

    signal statusPanelEvent(string message)
    signal reconnectVideo(int index)
    signal reconnectAllVideos()

    radius: 4
    color: "#e80b1017"
    border.color: "#384453"
    border.width: 1
    clip: true

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    function compactText(text, maxLen) {
        if (!text || text.length === 0) {
            return "--"
        }
        if (text.length <= maxLen) {
            return text
        }
        return text.substring(0, maxLen - 3) + "..."
    }

    function toggleMinimized() {
        minimized = !minimized
        statusPanelEvent(minimized ? qsTr("状态面板已最小化") : qsTr("状态面板已展开"))
    }

    function videoState(row) {
        if (!row.enabled) {
            return qsTr("已停用")
        }
        if (row.decoding || row.streaming) {
            return qsTr("在线")
        }
        if (row.status === "Connecting" || row.status === "Reconnecting") {
            return qsTr("连接中")
        }
        if (row.status === "Stopped" || row.status === "Failed") {
            return qsTr("已暂停")
        }
        return qsTr("离线")
    }

    function videoNeedsAction(row) {
        return row.enabled
               && (row.status === "Failed"
                   || row.status === "Stopped"
                   || row.status === "Waiting"
                   || row.watchdog === "Stalled")
    }

    function videoStateColor(row) {
        if (!row.enabled) {
            return "#94a3b8"
        }
        if (row.decoding || row.streaming) {
            return "#86efac"
        }
        if (row.status === "Connecting" || row.status === "Reconnecting") {
            return "#facc15"
        }
        return "#fca5a5"
    }

    function videoIssueText(row) {
        if (row.watchdog === "Stalled" || row.status === "Stalled") {
            return qsTr("视频长时间没有新画面")
        }
        if (row.status === "Failed") {
            return row.lastError.length > 0
                    ? compactText(row.lastError, 42)
                    : qsTr("自动重连次数已用尽")
        }
        if (row.status === "Stopped") {
            return qsTr("视频已暂停，可手动重连")
        }
        return row.url.length > 0 ? qsTr("正在等待视频流") : qsTr("尚未配置 RTSP URL")
    }

    function diagnosticWatchdogStatus(status) {
        if (status === "Stalled") {
            return qsTr("停滞")
        }
        if (status === "Disabled") {
            return qsTr("停用")
        }
        return qsTr("正常")
    }

    function layoutName(mode) {
        if (mode === "grid") {
            return qsTr("四宫格")
        }
        if (mode === "mainAux") {
            return qsTr("主辅画面")
        }
        if (mode === "panorama") {
            return qsTr("全景")
        }
        if (mode === "fullscreen") {
            return qsTr("全屏")
        }
        return qsTr("未知")
    }

    function hasReconnectableIssue() {
        for (var index = 0; index < videoRows.length; ++index) {
            if (videoNeedsAction(videoRows[index])) {
                return true
            }
        }
        return false
    }

    function aiState() {
        if (!aiRunning) {
            return qsTr("未运行")
        }
        if (!aiReceiverBound) {
            return qsTr("接收器故障")
        }
        return qsTr("运行中")
    }

    function aiStateColor() {
        if (!aiRunning) {
            return "#94a3b8"
        }
        return aiReceiverBound ? "#86efac" : "#fca5a5"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelWidth * 0.7

        RowLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("DeepShark 状态")
                color: "#f2f5f8"
                font.bold: true
                elide: Text.ElideRight
            }

            QGCButton {
                text: qsTr("全部重连")
                visible: root.hasReconnectableIssue()
                backgroundColor: "#86efac"
                textColor: "#064e3b"
                showBorder: true
                onClicked: root.reconnectAllVideos()
            }

            QGCButton {
                text: qsTr("最小化")
                onClicked: root.toggleMinimized()
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ColumnLayout {
                id: contentColumn
                width: parent.width
                spacing: ScreenTools.defaultFontPixelWidth

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth * 0.35

                    QGCLabel { text: qsTr("载具"); color: "#e5e7eb"; font.bold: true }
                    QGCLabel {
                        Layout.fillWidth: true
                        text: root.vehicleConnectionStatus
                        color: root.vehicleConnectionLevel === 1 ? "#86efac"
                                                                  : (root.vehicleConnectionLevel === 2 ? "#fca5a5" : "#94a3b8")
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    QGCLabel {
                        Layout.fillWidth: true
                        text: qsTr("模式：%1  ·  %2  ·  电量：%3")
                              .arg(root.vehicleFlightMode)
                              .arg(root.vehicleArmStatus)
                              .arg(root.vehicleBatteryStatus)
                        color: "#cbd5e1"
                        elide: Text.ElideRight
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#2f3742" }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth * 0.35

                    QGCLabel { text: qsTr("AI"); color: "#e5e7eb"; font.bold: true }
                    QGCLabel {
                        Layout.fillWidth: true
                        text: root.aiRunning && root.aiReceiverBound
                              ? qsTr("%1  ·  当前目标 %2 个").arg(root.aiState()).arg(root.aiDetectionCount)
                              : root.aiState()
                        color: root.aiStateColor()
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    QGCLabel {
                        Layout.fillWidth: true
                        visible: root.aiRunning && !root.aiReceiverBound && root.aiDetail.length > 0
                        text: root.compactText(root.aiDetail, 48)
                        color: "#fca5a5"
                        font.pointSize: ScreenTools.defaultFontPointSize * 0.72
                        elide: Text.ElideRight
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#2f3742" }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth * 0.45

                    QGCLabel { text: qsTr("视频"); color: "#e5e7eb"; font.bold: true }

                    Repeater {
                        model: root.videoRows

                        Rectangle {
                            Layout.fillWidth: true
                            height: Math.max(ScreenTools.defaultFontPixelHeight * 3.2, 50)
                            color: "#101820"
                            border.color: "#27313d"
                            radius: 3

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: ScreenTools.defaultFontPixelWidth * 0.55
                                spacing: ScreenTools.defaultFontPixelWidth * 0.5

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    RowLayout {
                                        Layout.fillWidth: true

                                        QGCLabel {
                                            Layout.fillWidth: true
                                            text: qsTr("%1. %2").arg(modelData.index).arg(modelData.name)
                                            color: "#f2f5f8"
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }

                                        QGCLabel {
                                            text: root.videoState(modelData)
                                            color: root.videoStateColor(modelData)
                                            font.bold: true
                                        }
                                    }

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        visible: root.videoNeedsAction(modelData)
                                        text: root.videoIssueText(modelData)
                                        color: "#fca5a5"
                                        font.pointSize: ScreenTools.defaultFontPointSize * 0.72
                                        elide: Text.ElideRight
                                    }
                                }

                                QGCButton {
                                    text: qsTr("重连")
                                    enabled: modelData.enabled
                                    visible: root.videoNeedsAction(modelData)
                                    backgroundColor: "#059669"
                                    textColor: "#ecfdf5"
                                    showBorder: true
                                    onClicked: root.reconnectVideo(modelData.index - 1)
                                }
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#2f3742" }

                QGCButton {
                    Layout.fillWidth: true
                    text: root.diagnosticsExpanded ? qsTr("收起诊断信息") : qsTr("展开诊断信息")
                    onClicked: root.diagnosticsExpanded = !root.diagnosticsExpanded
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: root.diagnosticsExpanded
                    spacing: ScreenTools.defaultFontPixelWidth * 0.35

                    QGCLabel { text: qsTr("诊断信息"); color: "#e5e7eb"; font.bold: true }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("布局：%1").arg(root.layoutName(root.layoutMode)); color: "#9ca3af"; elide: Text.ElideRight }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("主画面：%1 / %2").arg(root.mainIndex + 1).arg(root.mainName); color: "#9ca3af"; elide: Text.ElideRight }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("地图：%1  ·  面板：%2").arg(root.mapHidden ? qsTr("隐藏") : qsTr("显示")).arg(root.deepSharkPanelMinimized ? qsTr("最小化") : qsTr("显示")); color: "#9ca3af"; elide: Text.ElideRight }

                    Repeater {
                        model: root.rtspRows

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QGCLabel {
                                Layout.fillWidth: true
                                text: qsTr("%1. %2").arg(modelData.index).arg(root.compactText(modelData.url, 42))
                                color: "#9ca3af"
                                font.pointSize: ScreenTools.defaultFontPointSize * 0.7
                                elide: Text.ElideRight
                            }
                            QGCLabel {
                                Layout.fillWidth: true
                                text: qsTr("%1  ·  重试 %2  ·  watchdog %3/%4s  ·  %5  ·  %6")
                                      .arg(root.videoState(modelData))
                                      .arg(modelData.retry)
                                      .arg(root.diagnosticWatchdogStatus(modelData.watchdog))
                                      .arg(modelData.lastProgressAge >= 0 ? modelData.lastProgressAge : "--")
                                      .arg(modelData.fps || "FPS: --")
                                      .arg(modelData.latency || "Latency: --")
                                color: "#64748b"
                                font.pointSize: ScreenTools.defaultFontPointSize * 0.66
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; visible: root.diagnosticsExpanded; height: 1; color: "#2f3742" }

                QGCLabel { visible: root.diagnosticsExpanded; text: qsTr("最近事件"); color: "#e5e7eb"; font.bold: true }

                Rectangle {
                    Layout.fillWidth: true
                    visible: root.diagnosticsExpanded
                    height: Math.max(ScreenTools.defaultFontPixelHeight * 16, 220)
                    color: "#101820"
                    border.color: "#27313d"
                    radius: 3
                    clip: true

                    Flickable {
                        id: eventsFlickable
                        anchors.fill: parent
                        anchors.margins: ScreenTools.defaultFontPixelWidth * 0.6
                        contentWidth: width
                        contentHeight: eventsColumn.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        Column {
                            id: eventsColumn
                            width: eventsFlickable.width
                            spacing: ScreenTools.defaultFontPixelWidth * 0.35

                            Repeater {
                                model: root.recentEvents

                                QGCLabel {
                                    width: eventsColumn.width
                                    text: modelData
                                    color: "#9ca3af"
                                    font.pointSize: ScreenTools.defaultFontPointSize * 0.68
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                }
            }
        }
    }
}
