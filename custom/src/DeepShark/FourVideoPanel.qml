/****************************************************************************
 *
 * DeepShark four video placeholder panel.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import DeepShark 1.0
import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id: root

    radius: 4
    color: "#10151c"
    border.color: "#384453"
    border.width: 1
    opacity: 0.94

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelWidth

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: ScreenTools.defaultFontPixelHeight * 2
            spacing: ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("DeepShark Video Panel")
                color: "#f2f5f8"
                font.bold: true
                elide: Text.ElideRight
            }

            QGCLabel {
                text: qsTr("By KevinJiang")
                color: "#94a3b8"
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.85
            }

            QGCButton {
                text: qsTr("设置")
                onClicked: root.openSettings()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: ScreenTools.defaultFontPixelWidth
            columnSpacing: ScreenTools.defaultFontPixelWidth

            VideoTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: DeepSharkVideoSettings.camera1Name
                receiverName: "deepSharkVideo1"
                videoEnabled: DeepSharkVideoSettings.camera1Url.length > 0
                videoSource: DeepSharkVideoSettings.camera1Url
            }

            VideoTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: DeepSharkVideoSettings.camera2Name
                receiverName: "deepSharkVideo2"
                videoEnabled: DeepSharkVideoSettings.camera2Url.length > 0
                videoSource: DeepSharkVideoSettings.camera2Url
            }

            VideoTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: DeepSharkVideoSettings.camera3Name
                receiverName: "deepSharkVideo3"
                videoEnabled: DeepSharkVideoSettings.camera3Url.length > 0
                videoSource: DeepSharkVideoSettings.camera3Url
            }

            VideoTile {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: DeepSharkVideoSettings.camera4Name
                receiverName: "deepSharkVideo4"
                videoEnabled: DeepSharkVideoSettings.camera4Url.length > 0
                videoSource: DeepSharkVideoSettings.camera4Url
            }
        }
    }

    function openSettings() {
        camera1NameField.text = DeepSharkVideoSettings.camera1Name
        camera1UrlField.text = DeepSharkVideoSettings.camera1Url
        camera2NameField.text = DeepSharkVideoSettings.camera2Name
        camera2UrlField.text = DeepSharkVideoSettings.camera2Url
        camera3NameField.text = DeepSharkVideoSettings.camera3Name
        camera3UrlField.text = DeepSharkVideoSettings.camera3Url
        camera4NameField.text = DeepSharkVideoSettings.camera4Name
        camera4UrlField.text = DeepSharkVideoSettings.camera4Url
        settingsOverlay.visible = true
    }

    function saveSettings() {
        DeepSharkVideoSettings.setCamera(1, camera1NameField.text, camera1UrlField.text)
        DeepSharkVideoSettings.setCamera(2, camera2NameField.text, camera2UrlField.text)
        DeepSharkVideoSettings.setCamera(3, camera3NameField.text, camera3UrlField.text)
        DeepSharkVideoSettings.setCamera(4, camera4NameField.text, camera4UrlField.text)
        settingsOverlay.visible = false
    }

    Rectangle {
        id: settingsOverlay
        anchors.fill: parent
        visible: false
        z: 10
        color: "#d910151c"
        border.color: "#526174"
        border.width: 1
        radius: 4

        MouseArea {
            anchors.fill: parent
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: ScreenTools.defaultFontPixelWidth * 1.5
            spacing: ScreenTools.defaultFontPixelWidth

            RowLayout {
                Layout.fillWidth: true

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("DeepShark 视频设置")
                    color: "#f2f5f8"
                    font.bold: true
                }

                QGCButton {
                    text: qsTr("取消")
                    onClicked: settingsOverlay.visible = false
                }

                QGCButton {
                    text: qsTr("保存")
                    onClicked: root.saveSettings()
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 3
                columnSpacing: ScreenTools.defaultFontPixelWidth
                rowSpacing: ScreenTools.defaultFontPixelWidth * 0.7

                QGCLabel { text: qsTr("通道"); color: "#cbd5e1"; font.bold: true }
                QGCLabel { text: qsTr("名称"); color: "#cbd5e1"; font.bold: true }
                QGCLabel { text: qsTr("RTSP URL"); color: "#cbd5e1"; font.bold: true }

                QGCLabel { text: "1"; color: "#cbd5e1" }
                QGCTextField { id: camera1NameField; Layout.fillWidth: true }
                QGCTextField { id: camera1UrlField; Layout.fillWidth: true }

                QGCLabel { text: "2"; color: "#cbd5e1" }
                QGCTextField { id: camera2NameField; Layout.fillWidth: true }
                QGCTextField { id: camera2UrlField; Layout.fillWidth: true }

                QGCLabel { text: "3"; color: "#cbd5e1" }
                QGCTextField { id: camera3NameField; Layout.fillWidth: true }
                QGCTextField { id: camera3UrlField; Layout.fillWidth: true }

                QGCLabel { text: "4"; color: "#cbd5e1" }
                QGCTextField { id: camera4NameField; Layout.fillWidth: true }
                QGCTextField { id: camera4UrlField; Layout.fillWidth: true }
            }
        }
    }
}
