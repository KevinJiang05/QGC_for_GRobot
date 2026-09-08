import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DeepShark 1.0
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root
    property var monitor: DeepSharkConnectionMonitor
    readonly property real padding: ScreenTools.defaultFontPixelWidth
    visible: monitor.enabled || monitor.attentionRequired
    implicitHeight: ScreenTools.toolbarHeight * 0.72
    readonly property string summaryText: monitor.attentionRequired
                                         ? (monitor.alarmActive ? (monitor.acknowledged ? qsTr("异常断联（已静音）· ") : qsTr("异常断联 · ")) : "") + monitor.alarmText
                                         : monitor.statusText
    color: monitor.alarmActive ? "#862b2b" : (monitor.attentionRequired ? "#645124" : "#202a31")
    radius: 4
    clip: true
    Accessible.role: Accessible.Button
    Accessible.name: summaryText
    Accessible.onPressAction: details.open()
    QGCLabel {
        anchors.fill: parent
        anchors.margins: root.padding / 2
        text: root.summaryText
        color: "white"
        font.bold: root.monitor.attentionRequired
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    MouseArea {
        anchors.fill: parent
        onClicked: details.open()
    }

    // Full details appear only when requested; the toolbar itself remains one line.
    Popup {
        id: details
        parent: Overlay.overlay
        width: Math.min(ScreenTools.defaultFontPixelWidth * 60, parent ? parent.width - 16 : 640)
        x: parent ? Math.max(8, Math.min(root.mapToItem(parent, 0, 0).x, parent.width - width - 8)) : 0
        y: parent ? root.mapToItem(parent, 0, root.height).y + 4 : 0
        focus: true
        background: Rectangle { color: root.color; border.color: "#86949f"; radius: 4 }
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        contentItem: ColumnLayout {
        id: content
        spacing: root.padding / 2
        QGCLabel {
            Layout.fillWidth: true
            text: root.monitor.attentionRequired ? root.monitor.alarmText : root.monitor.statusText
            color: "white"
            font.bold: root.monitor.attentionRequired
            wrapMode: Text.WordWrap
        }
        QGCLabel {
            Layout.fillWidth: true
            visible: root.monitor.alarmActive
            text: qsTr("异常断联：可能存在通信、供电或进水问题，请立即检查并按现场流程处置。")
            color: "white"
            wrapMode: Text.WordWrap
        }
        RowLayout {
            visible: root.monitor.attentionRequired
            Layout.fillWidth: true
            QGCButton {
                text: root.monitor.alarmActive ? (root.monitor.acknowledged ? qsTr("已静音") : qsTr("确认并静音")) : qsTr("确认恢复记录")
                enabled: !root.monitor.alarmActive || !root.monitor.acknowledged
                onClicked: { root.monitor.acknowledge(); details.close() }
            }
            QGCLabel {
                Layout.fillWidth: true
                text: root.monitor.audioStatus
                color: "white"
                wrapMode: Text.WordWrap
            }
        }
        }
    }
}
