import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import DeepShark 1.0
import QGroundControl.Controls
import QGroundControl.ScreenTools

SettingsGroupLayout {
    id: root

    heading:            qsTr("AI Detection")
    headingDescription: qsTr("Optional local YOLO environment. QGC starts the configured Python bridge and receives detections over UDP.")

    property real _aiFieldWidth: ScreenTools.defaultFontPixelWidth * 48

    function syncAIDetectionPort() {
        AIDetectionReceiver.port = AIDetectionManager.udpPort
    }

    function saveAIDetectionSettings() {
        AIDetectionManager.pythonPath = pythonPathField.text
        AIDetectionManager.modelPath = modelPathField.text
        AIDetectionManager.device = aiDeviceField.text
        var confidence = parseFloat(aiConfidenceField.text)
        var imageSize = parseInt(aiImageSizeField.text)
        var maxFps = parseFloat(aiMaxFpsField.text)
        var udpPort = parseInt(aiUdpPortField.text)
        if (!isNaN(confidence)) {
            AIDetectionManager.confidence = confidence
        }
        if (!isNaN(imageSize)) {
            AIDetectionManager.imageSize = imageSize
        }
        if (!isNaN(maxFps)) {
            AIDetectionManager.maxFps = maxFps
        }
        if (!isNaN(udpPort)) {
            AIDetectionManager.udpPort = udpPort
        }
        syncAIDetectionPort()
        AIDetectionManager.saveSettings()
    }

    Component.onCompleted: syncAIDetectionPort()

    QGCCheckBoxSlider {
        Layout.fillWidth:   true
        text:               qsTr("YOLO Detection Overlay")
        onClicked:          AIDetectionManager.overlayEnabled = checked

        Binding on checked {
            value: AIDetectionManager.overlayEnabled
        }
    }

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth * 2

        QGCLabel {
            Layout.fillWidth: true
            text:             qsTr("Python")
        }

        QGCTextField {
            id:                     pythonPathField
            Layout.preferredWidth:  root._aiFieldWidth
            text:                   AIDetectionManager.pythonPath
            placeholderText:        qsTr("Example: C:/Python311/python.exe")
            onEditingFinished:      AIDetectionManager.pythonPath = text
        }

        QGCButton {
            text:       qsTr("Browse")
            onClicked:  pythonBrowseDialog.openForLoad()
            QGCFileDialog {
                id:                 pythonBrowseDialog
                title:              qsTr("Choose Python executable")
                folder:             AIDetectionManager.pythonPath
                selectFolder:       false
                onAcceptedForLoad:  (file) => AIDetectionManager.pythonPath = file
            }
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               qsTr("Select the Python executable from the local AI environment. It must include ultralytics and torch.")
        wrapMode:           Text.WordWrap
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth * 2

        QGCLabel {
            Layout.fillWidth: true
            text:             qsTr("Model")
        }

        QGCTextField {
            id:                     modelPathField
            Layout.preferredWidth:  root._aiFieldWidth
            text:                   AIDetectionManager.modelPath
            placeholderText:        qsTr("Example: D:/Models/best.pt")
            onEditingFinished:      AIDetectionManager.modelPath = text
        }

        QGCButton {
            text:       qsTr("Browse")
            onClicked:  modelBrowseDialog.openForLoad()
            QGCFileDialog {
                id:                 modelBrowseDialog
                title:              qsTr("Choose YOLO model file")
                folder:             AIDetectionManager.modelPath
                selectFolder:       false
                onAcceptedForLoad:  (file) => AIDetectionManager.modelPath = file
            }
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               qsTr("Select the local YOLO model file, for example a .pt file trained for this project.")
        wrapMode:           Text.WordWrap
        font.pointSize:     ScreenTools.smallFontPointSize
    }

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth * 2

        QGCLabel {
            Layout.fillWidth: true
            text:             qsTr("Device")
        }

        QGCTextField {
            id:                     aiDeviceField
            Layout.preferredWidth:  root._aiFieldWidth
            text:                   AIDetectionManager.device
            onEditingFinished:      AIDetectionManager.device = text
        }
    }

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth * 2

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Confidence")
        }

        QGCTextField {
            id:                    aiConfidenceField
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10
            text:                  AIDetectionManager.confidence.toFixed(2)
            inputMethodHints:      Qt.ImhFormattedNumbersOnly
            onEditingFinished:     AIDetectionManager.confidence = parseFloat(text)
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Image size")
        }

        QGCTextField {
            id:                    aiImageSizeField
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10
            text:                  AIDetectionManager.imageSize.toString()
            inputMethodHints:      Qt.ImhDigitsOnly
            onEditingFinished: {
                var value = parseInt(text)
                if (!isNaN(value)) {
                    AIDetectionManager.imageSize = value
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth * 2

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("Max FPS")
        }

        QGCTextField {
            id:                    aiMaxFpsField
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10
            text:                  AIDetectionManager.maxFps.toFixed(1)
            inputMethodHints:      Qt.ImhFormattedNumbersOnly
            onEditingFinished:     AIDetectionManager.maxFps = parseFloat(text)
        }

        QGCLabel {
            Layout.fillWidth:   true
            text:               qsTr("UDP port")
        }

        QGCTextField {
            id:                    aiUdpPortField
            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10
            text:                  AIDetectionManager.udpPort.toString()
            inputMethodHints:      Qt.ImhDigitsOnly
            onEditingFinished: {
                var value = parseInt(text)
                if (!isNaN(value)) {
                    AIDetectionManager.udpPort = value
                    root.syncAIDetectionPort()
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth:   true
        spacing:            ScreenTools.defaultFontPixelWidth * 2

        QGCButton {
            text:       qsTr("Check Environment")
            onClicked: {
                root.syncAIDetectionPort()
                AIDetectionManager.checkEnvironment()
            }
        }

        QGCButton {
            text:       AIDetectionManager.running ? qsTr("Stop AI") : qsTr("Start AI")
            onClicked: {
                root.syncAIDetectionPort()
                AIDetectionManager.running ? AIDetectionManager.stopDetection() : AIDetectionManager.startDetection()
            }
        }

        QGCButton {
            text:       qsTr("Restart")
            onClicked: {
                root.syncAIDetectionPort()
                AIDetectionManager.restartDetection()
            }
        }

        QGCButton {
            text:       qsTr("Save Settings")
            onClicked:  root.saveAIDetectionSettings()
        }
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               AIDetectionManager.statusText
        wrapMode:           Text.WordWrap
    }

    QGCLabel {
        Layout.fillWidth:   true
        text:               AIDetectionManager.checkReport + "\n" + qsTr("AI tools: ") + AIDetectionManager.toolsPath
        wrapMode:           Text.WordWrap
        font.pointSize:     ScreenTools.smallFontPointSize
    }
}
