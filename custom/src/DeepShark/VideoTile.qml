/****************************************************************************
 *
 * DeepShark video placeholder tile.
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

    radius: 4
    color: "#07090c"
    border.color: "#2f3742"
    border.width: 1
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

    Component.onCompleted: {
        statusLabel.text = _statusText()
    }

    Connections {
        target: videoController
        function onDecodingChanged() { statusLabel.text = root._statusText() }
        function onStreamingChanged() { statusLabel.text = root._statusText() }
        function onStatusTextChanged() { statusLabel.text = root._statusText() }
        function onStartAttemptsChanged() { statusLabel.text = root._statusText() }
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
              ? qsTr("stream:%1 decode:%2 tries:%3")
                    .arg(videoController.streaming)
                    .arg(videoController.decoding)
                    .arg(videoController.startAttempts)
              : ""
        color: "#6b7280"
        font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.7
        elide: Text.ElideRight
        visible: root.videoEnabled && !videoOutput.visible
    }
}
