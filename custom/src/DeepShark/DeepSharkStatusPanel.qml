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
    property real panelWidth: minimized ? Math.max(ScreenTools.defaultFontPixelWidth * 6, 44)
                                       : Math.max(ScreenTools.defaultFontPixelWidth * 36, 300)
    property string layoutMode: "grid"
    property int mainIndex: 0
    property string mainName: ""
    property bool mapHidden: false
    property bool deepSharkPanelMinimized: false
    property string vehicleStatus: qsTr("Unknown")
    property var rtspRows: []
    property var recentEvents: []

    signal statusPanelEvent(string message)

    width: panelWidth
    radius: 4
    color: "#e80b1017"
    border.color: "#384453"
    border.width: 1
    clip: true

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    function compactUrl(url) {
        if (!url || url.length === 0) {
            return "--"
        }
        if (url.length <= 38) {
            return url
        }
        return url.substring(0, 20) + "..." + url.substring(url.length - 14)
    }

    function toggleMinimized() {
        minimized = !minimized
        statusPanelEvent(minimized ? "Status panel minimized" : "Status panel expanded")
    }

    Item {
        anchors.fill: parent
        visible: root.minimized

        QGCButton {
            anchors.centerIn: parent
            text: qsTr("状态")
            onClicked: root.toggleMinimized()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelWidth * 0.8
        visible: !root.minimized

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
                text: qsTr("最小化")
                onClicked: root.toggleMinimized()
            }
        }

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
            spacing: ScreenTools.defaultFontPixelWidth * 0.35

            QGCLabel { text: qsTr("RTSP"); color: "#e5e7eb"; font.bold: true }

            Repeater {
                model: root.rtspRows

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("%1. %2").arg(modelData.index).arg(root.compactUrl(modelData.url))
                    color: "#9ca3af"
                    font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.72
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2f3742" }

        QGCLabel {
            Layout.fillWidth: true
            text: qsTr("Recent Events")
            color: "#e5e7eb"
            font.bold: true
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                            font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.68
                            wrapMode: Text.Wrap
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }
        }
    }
}
