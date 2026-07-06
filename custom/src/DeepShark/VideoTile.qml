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
    property string statusText: qsTr("Waiting for RTSP")
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
    property int maxAutoRetries: 5
    property string watchdogStatus: (!videoEnabled || videoSource.length === 0 || manualStopped || currentStatus === "Failed") ? "Disabled" : (stalled ? "Stalled" : "OK")
    property bool stalled: false
    property int watchdogReconnectCount: 0
    property double lastProgressTime: Date.now()
    property double lastWatchdogReconnectTime: 0
    property double lastWatchdogLogTime: 0
    property int lastFrameCount: 0
    property int lastProgressAgeSeconds: -1

    signal tileClicked()
    signal tileDoubleClicked()
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

    QGCVideoBackground {
        id: videoOutput
        anchors.fill: parent
        visible: root.videoEnabled && videoController.decoding
    }

    DeepSharkVideoController {
        id: videoController
        videoItem: videoOutput
        receiverName: root.receiverName
        uri: root.videoSource
        autoStart: root.controllerAutoStart
        lowLatency: true
    }

    Timer {
        id: restartTimer
        repeat: false
        onTriggered: {
            if (root.shuttingDown || root.manualStopped || !root.videoEnabled || root.videoSource.length === 0) {
                return
            }
            root.videoEvent(root.title + " Reconnecting retry=" + root.retryCount)
            root._setStatus("Connecting", "")
            videoController.start()
        }
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
            videoEvent(title + " watchdog OK")
        }
    }

    function _watchdogDisabledReason() {
        if (manualStopped) {
            return "manual stopped"
        }
        if (!videoEnabled || videoSource.length === 0) {
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
                videoEvent(title + " watchdog disabled: " + reason)
            }
            stalled = false
            return
        }

        if (currentStatus !== "Playing" && currentStatus !== "Streaming") {
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
        if (ageMs < 8000) {
            return
        }

        if (!stalled) {
            stalled = true
            _setStatus("Stalled", "No frame progress for " + Math.floor(ageMs / 1000) + "s")
            videoEvent(title + " stalled: no frame progress for " + Math.floor(ageMs / 1000) + "s")
        }

        if (Date.now() - lastWatchdogReconnectTime >= 15000 && retryCount < maxAutoRetries) {
            lastWatchdogReconnectTime = Date.now()
            watchdogReconnectCount++
            videoEvent(title + " watchdog reconnect")
            _scheduleReconnect("Watchdog stalled")
        }
    }

    function _setStatus(status, error) {
        if (error !== undefined && error !== null) {
            lastError = error
        }
        if (currentStatus === status) {
            return
        }
        currentStatus = status
        videoEvent(title + " " + status + (lastError.length > 0 && (status === "Failed" || status === "Waiting") ? ": " + lastError : ""))
    }

    function _scheduleReconnect(error) {
        if (shuttingDown || manualStopped || !videoEnabled || videoSource.length === 0) {
            return
        }

        retryCount++
        if (retryCount > maxAutoRetries) {
            _setStatus("Failed", error + " - manual reconnect required")
            return
        }

        var delayMs = retryCount === 1 ? 3000 : 5000
        if (currentStatus === "Stalled") {
            videoEvent(title + " Reconnecting retry=" + retryCount)
        }
        _setStatus("Reconnecting", error)
        videoController.stop()
        restartTimer.interval = delayMs
        restartTimer.restart()
    }

    function startVideo() {
        restartTimer.stop()
        stalled = false
        _markProgress()
        if (!videoEnabled || videoSource.length === 0) {
            manualStopped = false
            controllerAutoStart = false
            _setStatus("Waiting", "URL empty")
            videoEvent(title + " watchdog disabled: empty url")
            return
        }

        manualStopped = false
        retryCount = 0
        controllerAutoStart = true
        _setStatus("Connecting", "")
        videoController.start()
    }

    function stopVideo() {
        restartTimer.stop()
        stalled = false
        manualStopped = true
        controllerAutoStart = false
        videoController.stop()
        _setStatus("Stopped", "")
        videoEvent(title + " Stopped manually")
        videoEvent(title + " watchdog disabled: manual stopped")
    }

    function reconnectVideo() {
        restartTimer.stop()
        stalled = false
        _markProgress()
        manualStopped = false
        retryCount = 0
        controllerAutoStart = true
        _setStatus("Reconnecting", "")
        videoEvent(title + " Reconnect requested")
        videoController.stop()
        reconnectDelay.restart()
    }

    function restartVideo() {
        reconnectVideo()
    }

    Timer {
        id: reconnectDelay
        interval: 650
        repeat: false
        onTriggered: {
            if (!root.shuttingDown) {
                videoController.start()
            }
        }
    }

    function _syncForUrl() {
        restartTimer.stop()
        stalled = false
        _markProgress()
        retryCount = 0
        if (!videoEnabled || videoSource.length === 0) {
            manualStopped = false
            controllerAutoStart = false
            videoController.stop()
            _setStatus("Waiting", "URL empty")
            videoEvent(title + " watchdog disabled: empty url")
            return
        }
        if (!manualStopped) {
            videoEvent(title + " URL changed")
            controllerAutoStart = true
            _setStatus("Connecting", "")
            videoController.stop()
            reconnectDelay.restart()
        }
    }

    onVideoSourceChanged: _syncForUrl()
    onVideoEnabledChanged: _syncForUrl()

    Component.onCompleted: {
        if (videoEnabled && videoSource.length > 0) {
            _setStatus("Connecting", "")
        } else {
            _setStatus("Waiting", "URL empty")
        }
    }

    Component.onDestruction: {
        shuttingDown = true
        restartTimer.stop()
        watchdogTimer.stop()
        reconnectDelay.stop()
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
                decodeCount++
                root.retryCount = 0
                root._markProgress()
                root._setStatus("Playing", "")
            } else if (!root.manualStopped && root.currentStatus === "Playing") {
                root._scheduleReconnect("Decode stopped")
            }
        }

        function onStatusTextChanged() {
            var text = videoController.statusText
            if (text.indexOf("Start failed") >= 0 || text.indexOf("Decode failed") >= 0 || text.indexOf("unavailable") >= 0 || text.indexOf("not ready") >= 0) {
                root._scheduleReconnect(text)
            } else if (text.indexOf("Connecting") >= 0) {
                root._setStatus("Connecting", "")
            } else if (text.indexOf("Stopped") >= 0 && root.manualStopped) {
                root._setStatus("Stopped", "")
            }
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
        text: root.currentStatus
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
              ? qsTr("%1 | %2 | %3 | %4 wd:%5 age:%6s retry:%7")
                    .arg(root.resolutionText)
                    .arg(root.frameRateText)
                    .arg(root.latencyText)
                    .arg(root.currentStatus)
                    .arg(root.watchdogStatus)
                    .arg(root.lastProgressAgeSeconds < 0 ? "--" : root.lastProgressAgeSeconds)
                    .arg(root.retryCount)
              : qsTr("Waiting for RTSP")
        color: "#6b7280"
        font.pointSize: ScreenTools.defaultFontPointSize * 0.7
        elide: Text.ElideRight
        visible: true
    }

    AIDetectionVideoOverlay {
        anchors.fill: parent
        sourceId: root.receiverName
        videoWidth: videoController.videoWidth
        videoHeight: videoController.videoHeight
        showStatus: false
        visible: root.videoEnabled
                 && QGroundControl.settingsManager.videoSettings.yoloOverlay.rawValue
        z: 20
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: root.tileClicked()
        onDoubleClicked: root.tileDoubleClicked()
    }
}
