/****************************************************************************
 *
 * DeepShark video tile.
 *
 ****************************************************************************/

import QtQuick

import DeepShark 1.0
import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id: root
    objectName: videoEnabled ? receiverName : ""

    property string title: ""
    property bool videoEnabled: false
    property string videoSource: ""
    property string receiverName: "deepSharkVideo"
    property bool selected: false

    property string currentStatus: videoEnabled && videoSource.length > 0 ? "Connecting" : "Waiting"
    property string lastError: videoSource.length > 0 ? "" : "URL empty"
    property int retryCount: 0
    property int streamCount: 0
    property int decodeCount: 0
    property bool manualStopped: false
    property bool shuttingDown: false
    property bool controllerAutoStart: videoEnabled && videoSource.length > 0
    property bool streaming: videoController.streaming
    property bool decoding: videoController.decoding
    readonly property alias previewItem: videoOutput
    readonly property int videoWidth: videoController.videoWidth
    readonly property int videoHeight: videoController.videoHeight
    property string resolutionText: videoController.resolutionText
    property string frameRateText: videoController.frameRateText
    property string latencyText: videoController.latencyText
    property int estimatedLatencyMs: videoController.estimatedLatencyMs
    property int maxAutoRetries: 6
    property string watchdogStatus: (!videoEnabled || videoSource.length === 0 || manualStopped || currentStatus === "Failed") ? "Disabled" : (stalled ? "Stalled" : "OK")
    property bool stalled: false
    property bool dragEnabled: true
    property bool reconnectPending: false
    property int watchdogReconnectCount: 0
    property double lastProgressTime: Date.now()
    property double lastWatchdogReconnectTime: 0
    property double lastWatchdogLogTime: 0
    property int lastFrameCount: 0
    property int lastProgressAgeSeconds: -1

    signal tileClicked()
    signal tileDoubleClicked()
    signal tileDragStarted(real centerX, real centerY)
    signal tileDragMoved(real centerX, real centerY)
    signal tileDragReleased(real centerX, real centerY)
    signal videoEvent(string message)

    radius: 4
    color: "#07090c"
    border.color: selected ? "#facc15" : "#2f3742"
    border.width: selected ? 3 : 1
    clip: true

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    function displayStatus(status) {
        switch (status) {
        case "Connecting":
        case "Reconnecting":
            return qsTr("连接中")
        case "Streaming":
        case "Playing":
            return qsTr("在线")
        case "Stopped":
            return qsTr("已暂停")
        case "Failed":
            return qsTr("连接失败")
        case "Stalled":
            return qsTr("画面停滞")
        default:
            return qsTr("等待视频流")
        }
    }

    function displayWatchdogStatus(status) {
        if (status === "Stalled") {
            return qsTr("停滞")
        }
        if (status === "Disabled") {
            return qsTr("停用")
        }
        return qsTr("正常")
    }

    QGCVideoBackground {
        id: videoOutput
        anchors.fill: parent
        visible: root.videoEnabled && videoController.decoding
    }

    DeepSharkVideoController {
        id: videoController
        videoItem: root.videoEnabled ? videoOutput : null
        receiverName: root.receiverName
        uri: DeepSharkVideoSettings.streamUrl(root.videoSource, DeepSharkVideoSettings.rtspTransport)
        autoStart: root.controllerAutoStart
        alertExpected: root.videoEnabled && !root.manualStopped && !root.shuttingDown
        alertTitle: root.title
        lowLatency: true
    }

    Timer {
        id: watchdogTimer
        interval: 1000
        running: true
        repeat: true
        onTriggered: root._watchdogTick()
    }

    function _markProgress() {
        lastProgressTime = Date.now()
        if (stalled) {
            stalled = false
            videoEvent(qsTr("%1：watchdog 恢复正常").arg(title))
        }
        if (currentStatus === "Stalled" && !manualStopped) {
            if (videoController.decoding) {
                _setStatus("Playing", "")
            } else if (videoController.streaming) {
                _setStatus("Streaming", "")
            }
        }
    }

    function _watchdogDisabledReason() {
        if (manualStopped) {
            return "manual stopped"
        }
        if (!videoEnabled) {
            return videoSource.length > 0 ? "channel disabled" : "empty url"
        }
        if (videoSource.length === 0) {
            return "empty url"
        }
        if (currentStatus === "Failed") {
            return "failed retry limit"
        }
        return ""
    }

    function _watchdogTick() {
        var reason = _watchdogDisabledReason()
        if (reason.length > 0) {
            lastProgressAgeSeconds = -1
            if (watchdogStatus !== "Disabled") {
                videoEvent(qsTr("%1：watchdog 已停用").arg(title))
            }
            stalled = false
            return
        }

        if (currentStatus !== "Playing" && currentStatus !== "Streaming" && currentStatus !== "Stalled") {
            lastProgressAgeSeconds = Math.max(0, Math.floor((Date.now() - lastProgressTime) / 1000))
            return
        }

        var frameCount = videoController.frameCount
        if (frameCount !== lastFrameCount) {
            lastFrameCount = frameCount
            _markProgress()
            lastProgressAgeSeconds = 0
            return
        }

        var ageMs = Date.now() - lastProgressTime
        lastProgressAgeSeconds = Math.floor(ageMs / 1000)
        var stallTimeoutMs = videoController.streaming && !videoController.decoding ? 5000 : 8000
        if (ageMs < stallTimeoutMs) {
            return
        }

        if (!stalled) {
            stalled = true
            var stalledReason = videoController.streaming && !videoController.decoding
                    ? "Streaming without decoded frames for "
                    : "No frame progress for "
            _setStatus("Stalled", stalledReason + Math.floor(ageMs / 1000) + "s")
            videoEvent(qsTr("%1：画面停滞 %2 秒").arg(title).arg(Math.floor(ageMs / 1000)))
        }

        if (Date.now() - lastWatchdogReconnectTime >= 15000 && _canAutoRetry()) {
            lastWatchdogReconnectTime = Date.now()
            watchdogReconnectCount++
            videoEvent(qsTr("%1：watchdog 请求重连").arg(title))
            _scheduleReconnect("Watchdog stalled")
        }
    }

    function _canAutoRetry() {
        return maxAutoRetries <= 0 || retryCount < maxAutoRetries
    }

    function _setStatus(status, error) {
        if (error !== undefined && error !== null) {
            lastError = error
        }
        if (currentStatus === status) {
            return
        }
        currentStatus = status
        videoEvent(qsTr("%1：%2").arg(title).arg(displayStatus(status)))
    }

    function _scheduleReconnect(error) {
        if (shuttingDown || manualStopped || !videoEnabled || videoSource.length === 0) {
            return
        }
        if (reconnectPending) {
            return
        }

        retryCount++
        if (maxAutoRetries > 0 && retryCount > maxAutoRetries) {
            _setStatus("Failed", error)
            return
        }

        var delayMs = Math.min(30000, 2500 * Math.pow(2, retryCount - 1))
        if (currentStatus === "Stalled") {
            videoEvent(qsTr("%1：正在重连，第 %2 次").arg(title).arg(retryCount))
        }
        _setStatus("Reconnecting", error)
        reconnectPending = true
        videoController.restart(delayMs)
    }

    function startVideo() {
        stalled = false
        reconnectPending = false
        _markProgress()
        if (!videoEnabled || videoSource.length === 0) {
            manualStopped = false
            controllerAutoStart = false
            var startReason = videoSource.length > 0 ? "Channel disabled" : "URL empty"
            _setStatus("Waiting", startReason)
            videoEvent(qsTr("%1：无法启动，通道停用或 URL 为空").arg(title))
            return
        }

        manualStopped = false
        retryCount = 0
        controllerAutoStart = true
        _setStatus("Connecting", "")
        videoController.start()
    }

    function stopVideo() {
        stalled = false
        reconnectPending = false
        manualStopped = true
        controllerAutoStart = false
        videoController.stop()
        _setStatus("Stopped", "")
        videoEvent(qsTr("%1：已手动停止").arg(title))
    }

    function reconnectVideo() {
        DeepSharkConnectionMonitor.manualReconnect(videoController)
        stalled = false
        _markProgress()
        manualStopped = false
        retryCount = 0
        controllerAutoStart = true
        _setStatus("Reconnecting", "")
        videoEvent(qsTr("%1：已请求重连").arg(title))
        reconnectPending = true
        videoController.restart(2500)
    }

    function restartVideo() {
        reconnectVideo()
    }

    function _syncForUrl() {
        stalled = false
        reconnectPending = false
        _markProgress()
        retryCount = 0
        if (!videoEnabled || videoSource.length === 0) {
            manualStopped = false
            controllerAutoStart = false
            videoController.stop()
            var syncReason = videoSource.length > 0 ? "Channel disabled" : "URL empty"
            _setStatus("Waiting", syncReason)
            videoEvent(qsTr("%1：通道停用或 URL 为空").arg(title))
            return
        }
        if (!manualStopped) {
            videoEvent(qsTr("%1：URL 已更新").arg(title))
            controllerAutoStart = true
            _setStatus("Connecting", "")
            videoController.restart(650)
        }
    }

    onVideoSourceChanged: _syncForUrl()
    onVideoEnabledChanged: _syncForUrl()

    Component.onCompleted: {
        DeepSharkConnectionMonitor.track(videoController)
        if (videoEnabled && videoSource.length > 0) {
            _setStatus("Connecting", "")
        } else {
            _setStatus("Waiting", videoSource.length > 0 ? "Channel disabled" : "URL empty")
        }
    }

    Component.onDestruction: {
        shuttingDown = true
        watchdogTimer.stop()
        reconnectPending = false
        manualStopped = true
        controllerAutoStart = false
        videoController.stop()
    }

    Connections {
        target: videoController

        function onStreamingChanged() {
            if (videoController.streaming) {
                streamCount++
                root._markProgress()
                if (!videoController.decoding) {
                    root._setStatus("Streaming", "")
                }
            } else if (!root.manualStopped && root.currentStatus !== "Waiting" && root.currentStatus !== "Failed") {
                root._scheduleReconnect("Stream stopped")
            }
        }

        function onDecodingChanged() {
            if (videoController.decoding) {
                root.reconnectPending = false
                decodeCount++
                root.retryCount = 0
                root._markProgress()
                root._setStatus("Playing", "")
            } else if (!root.manualStopped && root.currentStatus === "Playing") {
                root._scheduleReconnect("Decode stopped")
            }
        }

        function onFailure(message) {
            root.reconnectPending = false
            root._scheduleReconnect(message)
        }

        function onStartAttemptsChanged() {
            // The delayed restart has now started. Keep the guard only for the
            // stop/backoff window so a later watchdog stall can retry again.
            root.reconnectPending = false
        }

        function onFrameCountChanged() {
            root._markProgress()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(ScreenTools.defaultFontPixelHeight * 1.8, 26)
        color: "#141920"
        opacity: 0.96

        QGCLabel {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth
            text: root.title
            color: "#f2f5f8"
            elide: Text.ElideRight
            font.bold: true
        }
    }

    QGCLabel {
        anchors.centerIn: parent
        text: root.displayStatus(root.currentStatus)
        color: "#9aa6b2"
        font.pointSize: ScreenTools.defaultFontPointSize * 0.9
        visible: !videoOutput.visible
    }

    QGCLabel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: ScreenTools.defaultFontPixelWidth
        text: root.videoEnabled
              ? qsTr("%1 | %2 | %3 | %4 | watchdog：%5 | 画面年龄：%6s | 重试：%7")
                    .arg(root.resolutionText)
                    .arg(root.frameRateText)
                    .arg(root.latencyText)
                    .arg(root.displayStatus(root.currentStatus))
                    .arg(root.displayWatchdogStatus(root.watchdogStatus))
                    .arg(root.lastProgressAgeSeconds < 0 ? "--" : root.lastProgressAgeSeconds)
                    .arg(root.retryCount)
              : qsTr("等待 RTSP")
        color: "#6b7280"
        font.pointSize: ScreenTools.defaultFontPointSize * 0.7
        elide: Text.ElideRight
        visible: true
    }

    Loader {
        anchors.fill: parent
        active: root.videoEnabled
                && QGroundControl.settingsManager.videoSettings.yoloOverlay.rawValue
        visible: active
        z: 20
        sourceComponent: AIDetectionVideoOverlay {
            anchors.fill: parent
            sourceId: root.receiverName
            videoWidth: videoController.videoWidth
            videoHeight: videoController.videoHeight
            showStatus: false
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real pressX: 0
        property real pressY: 0
        property bool dragging: false

        function pointInParent(mouse) {
            return root.mapToItem(root.parent, mouse.x, mouse.y)
        }

        onPressed: function(mouse) {
            pressX = mouse.x
            pressY = mouse.y
            dragging = false
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root.dragEnabled) {
                return
            }
            var dx = mouse.x - pressX
            var dy = mouse.y - pressY
            if (!dragging && Math.sqrt(dx * dx + dy * dy) >= 10) {
                dragging = true
                var startPoint = pointInParent(mouse)
                root.tileDragStarted(startPoint.x, startPoint.y)
            }
            if (dragging) {
                var movePoint = pointInParent(mouse)
                root.tileDragMoved(movePoint.x, movePoint.y)
            }
        }
        onReleased: function(mouse) {
            if (dragging) {
                var releasePoint = pointInParent(mouse)
                root.tileDragReleased(releasePoint.x, releasePoint.y)
                dragging = false
            } else {
                root.tileClicked()
            }
        }
        onDoubleClicked: function(mouse) {
            if (!dragging) {
                root.tileDoubleClicked()
            }
        }
    }
}
