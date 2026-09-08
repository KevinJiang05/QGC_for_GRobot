import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DeepShark 1.0
import QGroundControl.Controls
import QGroundControl.ScreenTools

SettingsGroupLayout {
    id: root
    heading: qsTr("异常断联预警")
    headingDescription: qsTr("飞控和已启用的视频曾稳定连接后，检测心跳中断或视频无新帧。断联不等于进水，请按现场流程处置。设置自动保存。")
    property var monitor: DeepSharkConnectionMonitor

    QGCCheckBox {
        text: qsTr("启用异常断联预警")
        checked: root.monitor.enabled
        onClicked: root.monitor.enabled = checked
    }
    QGCLabel {
        Layout.fillWidth: true
        text: root.monitor.statusText
        wrapMode: Text.WordWrap
    }
    Repeater {
        model: [
            { key: "videoTimeout", label: qsTr("视频无新帧超时（秒）"), low: 2, high: 30 },
            { key: "heartbeatTimeout", label: qsTr("飞控心跳超时（秒）"), low: 2, high: 30 },
            { key: "readySeconds", label: qsTr("初始稳定连接时间（秒）"), low: 3, high: 60 },
            { key: "recoverySeconds", label: qsTr("恢复确认时间（秒）"), low: 2, high: 30 },
            { key: "repeatSeconds", label: qsTr("提示音重复间隔（秒）"), low: 2, high: 30 }
        ]
        RowLayout {
            Layout.fillWidth: true
            enabled: root.monitor.enabled
            QGCLabel { text: modelData.label; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            SpinBox {
                from: modelData.low
                to: modelData.high
                value: root.monitor[modelData.key]
                editable: true
                onValueModified: root.monitor[modelData.key] = value
            }
        }
    }
    QGCLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("每 0.5 秒检查一次。以上阈值仅用于本预警，不修改飞控或原有失联判定。手动重连给予该通道 5 秒宽限；自动重连不会清除报警。")
    }
    QGCCheckBox {
        text: qsTr("开启重复提示音")
        checked: root.monitor.soundEnabled
        onClicked: root.monitor.soundEnabled = checked
    }
    RowLayout {
        Layout.fillWidth: true
        QGCLabel { text: qsTr("警报音类型"); Layout.fillWidth: true }
        QGCComboBox {
            model: [qsTr("短提示音"), qsTr("Hi-Low 警笛")]
            currentIndex: root.monitor.soundType
            onActivated: function(index) { root.monitor.soundType = index }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        QGCButton {
            text: qsTr("声音测试")
            enabled: root.monitor.soundEnabled
            onClicked: root.monitor.testSound()
        }
        QGCLabel { text: root.monitor.audioStatus; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    }
    QGCLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("提示音遵循应用总静音和系统音量。首次使用或切换声音设备后请测试。最小化窗口仍监测；不能识别持续发送重复画面的摄像头故障。")
    }
    QGCLabel {
        Layout.fillWidth: true
        visible: root.monitor.attentionRequired
        text: root.monitor.alarmText
        color: "#ef7070"
        wrapMode: Text.WordWrap
    }
    QGCButton {
        visible: root.monitor.attentionRequired
        text: root.monitor.alarmActive ? qsTr("确认并静音") : qsTr("确认恢复记录")
        onClicked: root.monitor.acknowledge()
    }
}
