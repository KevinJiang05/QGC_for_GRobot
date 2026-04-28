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
    property string currentStatus: statusText
    property int retryCount: videoController.startAttempts
    property bool streaming: videoController.streaming
    property bool decoding: videoController.decoding
    property string resolutionText: videoController.resolutionText
    property string frameRateText: videoController.frameRateText

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
        autoStart: root.videoEnabled
        lowLatency: true
    }

    function _statusText() {
        if (!videoEnabled) {
            return statusText
        }
        if (videoSource.length === 0) {
            return qsTr("Set RTSP URL in Video Settings")
        }
        if (videoController.decoding) {
            return qsTr("Playing")
        }
        if (videoController.streaming) {
            return qsTr("Connecting")
        }
        return videoController.statusText
    }

    function _refreshStatus() {
        var newStatus = _statusText()
        statusLabel.text = newStatus
        if (currentStatus !== newStatus) {
            currentStatus = newStatus
            videoEvent(title + ": " + newStatus)
        }
    }

    Component.onCompleted: _refreshStatus()

    Connections {
        target: videoController
        function onDecodingChanged() { root._refreshStatus() }
        function onStreamingChanged() { root._refreshStatus() }
        function onStatusTextChanged() { root._refreshStatus() }
        function onStartAttemptsChanged() { root._refreshStatus() }
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
        id: statusLabel
        anchors.centerIn: parent
        text: root._statusText()
        color: "#9aa6b2"
        font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.9
        visible: !videoOutput.visible
    }

    QGCLabel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: ScreenTools.defaultFontPixelWidth
        text: root.videoEnabled
              ? qsTr("%1 | %2 | stream:%3 decode:%4 retry:%5")
                    .arg(videoController.resolutionText)
                    .arg(videoController.frameRateText)
                    .arg(videoController.streaming)
                    .arg(videoController.decoding)
                    .arg(videoController.startAttempts)
              : qsTr("Waiting for RTSP")
        color: "#6b7280"
        font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.7
        elide: Text.ElideRight
        visible: true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: root.tileClicked()
        onDoubleClicked: root.tileDoubleClicked()
    }
}
