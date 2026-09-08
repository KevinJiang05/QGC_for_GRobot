/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.FactSystem
import QGroundControl.FactControls
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.FlightDisplay
import QGroundControl.Controllers
import "qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark"

SettingsPage {
    property var    _settingsManager:            QGroundControl.settingsManager
    property var    _videoManager:              QGroundControl.videoManager
    property var    _videoSettings:             _settingsManager.videoSettings
    property string _videoSource:               _videoSettings.videoSource.rawValue
    property bool   _isGST:                     _videoManager.gstreamerEnabled
    property bool   _isStreamSource:            _videoManager.isStreamSource
    property bool   _isUDP264:                  _isStreamSource && (_videoSource === _videoSettings.udp264VideoSource)
    property bool   _isUDP265:                  _isStreamSource && (_videoSource === _videoSettings.udp265VideoSource)
    property bool   _isRTSP:                    _isStreamSource && (_videoSource === _videoSettings.rtspVideoSource)
    property bool   _isTCP:                     _isStreamSource && (_videoSource === _videoSettings.tcpVideoSource)
    property bool   _isMPEGTS:                  _isStreamSource && (_videoSource === _videoSettings.mpegtsVideoSource)
    property bool   _videoAutoStreamConfig:     _videoManager.autoStreamConfigured
    property bool   _videoSourceDisabled:       _videoSource === _videoSettings.disabledVideoSource
    property real   _urlFieldWidth:             ScreenTools.defaultFontPixelWidth * 40
    property bool   _requiresUDPUrl:            _isUDP264 || _isUDP265 || _isMPEGTS
    property real   _aiFieldWidth:              ScreenTools.defaultFontPixelWidth * 48

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

    ConnectionAlertSettings {
        Layout.fillWidth: true
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Video Source")
        headingDescription: _videoAutoStreamConfig ? qsTr("Mavlink camera stream is automatically configured") : ""
        enabled:            !_videoAutoStreamConfig

        LabelledFactComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Source")
            indexModel:         false
            fact:               _videoSettings.videoSource
            visible:            fact.visible
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Connection")
        visible:            !_videoSourceDisabled && !_videoAutoStreamConfig && (_isTCP || _isRTSP | _requiresUDPUrl)

        LabelledFactTextField {
            Layout.fillWidth:           true
            textFieldPreferredWidth:    _urlFieldWidth
            label:                      qsTr("RTSP URL")
            fact:                       _videoSettings.rtspUrl
            visible:                    _isRTSP && _videoSettings.rtspUrl.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:           true
            label:                      qsTr("TCP URL")
            textFieldPreferredWidth:    _urlFieldWidth
            fact:                       _videoSettings.tcpUrl
            visible:                    _isTCP && _videoSettings.tcpUrl.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:           true
            textFieldPreferredWidth:    _urlFieldWidth
            label:                      qsTr("UDP URL")
            fact:                       _videoSettings.udpUrl
            visible:                    _requiresUDPUrl && _videoSettings.udpUrl.visible
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("Settings")
        // Decoder priority also controls custom GStreamer receivers, so keep
        // this group available when QGC's built-in video source is disabled.
        visible:            !_videoSourceDisabled || (_isGST && _videoSettings.forceVideoDecoder.visible)

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Aspect Ratio")
            fact:               _videoSettings.aspectRatio
            visible:            !_videoAutoStreamConfig && _isStreamSource && _videoSettings.aspectRatio.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Stop recording when disarmed")
            fact:               _videoSettings.disableWhenDisarmed
            visible:            !_videoAutoStreamConfig && _isStreamSource && fact.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Low Latency Mode")
            fact:               _videoSettings.lowLatencyMode
            visible:            !_videoAutoStreamConfig && _isStreamSource && fact.visible && _isGST
        }

        LabelledFactComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Video decode priority")
            fact:               _videoSettings.forceVideoDecoder
            visible:            fact.visible
            indexModel:         false
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth:   true
        heading:            qsTr("AI Detection")
        headingDescription: qsTr("Optional local YOLO environment. QGC starts the configured Python bridge and receives detections over UDP.")

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("YOLO Detection Overlay")
            fact:               _videoSettings.yoloOverlay
            visible:            fact.visible
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
                Layout.preferredWidth:  _aiFieldWidth
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
                Layout.preferredWidth:  _aiFieldWidth
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
                Layout.preferredWidth:  _aiFieldWidth
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
                        syncAIDetectionPort()
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
                    syncAIDetectionPort()
                    AIDetectionManager.checkEnvironment()
                }
            }

            QGCButton {
                text:       AIDetectionManager.running ? qsTr("Stop AI") : qsTr("Start AI")
                onClicked: {
                    syncAIDetectionPort()
                    AIDetectionManager.running ? AIDetectionManager.stopDetection() : AIDetectionManager.startDetection()
                }
            }

            QGCButton {
                text:       qsTr("Restart")
                onClicked: {
                    syncAIDetectionPort()
                    AIDetectionManager.restartDetection()
                }
            }

            QGCButton {
                text:       qsTr("Save Settings")
                onClicked:  saveAIDetectionSettings()
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

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading:            qsTr("Local Video Storage")

        LabelledFactComboBox {
            Layout.fillWidth:   true
            label:              qsTr("Record File Format")
            fact:               _videoSettings.recordingFormat
            visible:            _videoSettings.recordingFormat.visible
        }

        FactCheckBoxSlider {
            Layout.fillWidth:   true
            text:               qsTr("Auto-Delete Saved Recordings")
            fact:               _videoSettings.enableStorageLimit
            visible:            fact.visible
        }

        LabelledFactTextField {
            Layout.fillWidth:   true
            label:              qsTr("Max Storage Usage")
            fact:               _videoSettings.maxVideoSize
            visible:            fact.visible
            enabled:            _videoSettings.enableStorageLimit.rawValue
        }
    }
}
