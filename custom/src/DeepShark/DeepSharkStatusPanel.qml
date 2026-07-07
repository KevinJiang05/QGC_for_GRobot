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
    property string vehicleStatus: qsTr("Unknown")
    property var videoRows: []
    property var rtspRows: videoRows
    property var recentEvents: []

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
        statusPanelEvent(minimized ? "Status panel minimized" : "Status panel expanded")
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
                text: qsTr("DeepShark Status")
                color: "#f2f5f8"
                font.bold: true
                elide: Text.ElideRight
            }

            QGCButton {
                text: qsTr("全部重连")
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

                    QGCLabel { text: qsTr("System"); color: "#e5e7eb"; font.bold: true }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("Layout: %1").arg(root.layoutMode); color: "#cbd5e1"; elide: Text.ElideRight }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("Main: %1 / %2").arg(root.mainIndex + 1).arg(root.mainName); color: "#cbd5e1"; elide: Text.ElideRight }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("Map: %1").arg(root.mapHidden ? "Hidden" : "Visible"); color: "#cbd5e1"; elide: Text.ElideRight }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("Panel: %1").arg(root.deepSharkPanelMinimized ? "Minimized" : "Visible"); color: "#cbd5e1"; elide: Text.ElideRight }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("Vehicle: %1").arg(root.vehicleStatus); color: "#cbd5e1"; elide: Text.ElideRight }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#2f3742" }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth * 0.45

                    QGCLabel { text: qsTr("Video"); color: "#e5e7eb"; font.bold: true }

                    Repeater {
                        model: root.videoRows

                        Rectangle {
                            Layout.fillWidth: true
                            height: Math.max(ScreenTools.defaultFontPixelHeight * 4.6, 72)
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

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: qsTr("%1. %2").arg(modelData.index).arg(modelData.name)
                                        color: "#f2f5f8"
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 | retry:%2 | watchdog:%3 age:%4")
                                              .arg(modelData.enabled ? modelData.status : "Disabled")
                                              .arg(modelData.retry)
                                              .arg(modelData.watchdog || "Disabled")
                                              .arg(modelData.lastProgressAge >= 0 ? modelData.lastProgressAge + "s" : "--")
                                        color: "#9ca3af"
                                        font.pointSize: ScreenTools.defaultFontPointSize * 0.7
                                        elide: Text.ElideRight
                                    }

                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 | %2 | wd-reconnect:%3 | err:%4")
                                              .arg(modelData.fps || "FPS: --")
                                              .arg(modelData.latency || "Latency: --")
                                              .arg(modelData.watchdogReconnectCount || 0)
                                              .arg(root.compactText(modelData.lastError, 24))
                                        color: "#9ca3af"
                                        font.pointSize: ScreenTools.defaultFontPointSize * 0.7
                                        elide: Text.ElideRight
                                    }
                                }

                                QGCButton {
                                    text: qsTr("重连")
                                    enabled: modelData.enabled
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth * 0.35

                    QGCLabel { text: qsTr("RTSP"); color: "#e5e7eb"; font.bold: true }

                    Repeater {
                        model: root.rtspRows

                        QGCLabel {
                            Layout.fillWidth: true
                            text: qsTr("%1. %2").arg(modelData.index).arg(root.compactText(modelData.url, 42))
                            color: "#9ca3af"
                            font.pointSize: ScreenTools.defaultFontPointSize * 0.7
                            elide: Text.ElideRight
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: "#2f3742" }

                QGCLabel { text: qsTr("Recent Events"); color: "#e5e7eb"; font.bold: true }

                Rectangle {
                    Layout.fillWidth: true
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
