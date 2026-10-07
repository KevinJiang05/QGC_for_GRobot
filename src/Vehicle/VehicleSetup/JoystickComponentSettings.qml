import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.VehicleSetup
import QGroundControl.FactControls

ColumnLayout {
    spacing: _margins

    required property var joystick

    readonly property var _joystickSettings: joystick ? joystick.settings : null
    readonly property real _margins: ScreenTools.defaultFontPixelHeight / 2

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: _margins
        Layout.rightMargin: _margins
        spacing: _margins

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Center stick is zero throttle")
            fact: _joystickSettings ? _joystickSettings.throttleModeCenterZero : null
            visible: fact && fact.userVisible
        }

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Spring loaded throttle smoothing")
            fact: _joystickSettings ? _joystickSettings.throttleSmoothing : null
            visible: fact && fact.userVisible && _joystickSettings.throttleModeCenterZero.rawValue
        }

        FactTextFieldSlider {
            Layout.fillWidth: true
            label: fact ? fact.shortDescription : ""
            fact: _joystickSettings ? _joystickSettings.exponentialPct : null
        }

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Negative Thrust")
            fact: _joystickSettings ? _joystickSettings.negativeThrust : null
            visible: QGroundControl.multiVehicleManager.activeVehicle
                     && QGroundControl.multiVehicleManager.activeVehicle.supports.negativeThrust && fact && fact.userVisible
        }
    }

    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Advanced Settings")

        FactCheckBoxSlider {
            Layout.fillWidth: true
            text: qsTr("Circle Correction")
            fact: _joystickSettings ? _joystickSettings.circleCorrection : null
            visible: fact && fact.userVisible
        }

        FactTextFieldSlider {
            Layout.fillWidth: true
            label: fact ? fact.shortDescription : ""
            fact: _joystickSettings ? _joystickSettings.axisFrequencyHz : null
            visible: fact && fact.userVisible
        }

        FactTextFieldSlider {
            Layout.fillWidth: true
            label: fact ? fact.shortDescription : ""
            fact: _joystickSettings ? _joystickSettings.buttonFrequencyHz : null
            visible: fact && fact.userVisible
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            FactCheckBoxSlider {
                text: qsTr("Deadband")
                fact: _joystickSettings ? _joystickSettings.useDeadband : null
                visible: fact && fact.userVisible
            }

            QGCLabel{
                Layout.fillWidth: true
                Layout.maximumWidth: additionalAxesRcChannelsOverride.x + additionalAxesRcChannelsOverride.width
                font.pointSize: ScreenTools.smallFontPointSize
                wrapMode: Text.WordWrap
                text: qsTr("Deadband can be set during the first step of calibration by gently wiggling each axis. ")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCLabel { text: qsTr("MANUAL_CONTROL Extensions") }

            ColumnLayout {
                Layout.leftMargin: ScreenTools.defaultFontPixelWidth
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth / 2

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: qsTr("Pitch")
                    fact: _joystickSettings ? _joystickSettings.enableManualControlPitchExtension : null
                    visible: fact && fact.userVisible
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: qsTr("Roll")
                    fact: _joystickSettings ? _joystickSettings.enableManualControlRollExtension : null
                    visible: fact && fact.userVisible
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelWidth / 2

            QGCLabel { text: qsTr("Additional Axes") }

            ColumnLayout {
                Layout.leftMargin: ScreenTools.defaultFontPixelWidth
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth / 2

                ColumnLayout {
                    spacing: 0

                    QGCRadioButton {
                        id: additionalAxesManualControl
                        text: qsTr("Send using MANUAL_CONTROL")
                        checked: _joystickSettings && _joystickSettings.additionalAxesFunction.rawValue == 0
                        onClicked: _joystickSettings.additionalAxesFunction.rawValue = 0
                    }

                    QGCRadioButton {
                        id: additionalAxesRcChannelsOverride
                        text: qsTr("Send using RC_CHANNELS_OVERRIDE")
                        checked: _joystickSettings && _joystickSettings.additionalAxesFunction.rawValue == 1
                        onClicked: _joystickSettings.additionalAxesFunction.rawValue = 1
                    }
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: additionalAxesManualControl.checked ? qsTr("Aux1") : qsTr("Channel 5")
                    fact: _joystickSettings ? _joystickSettings.enableAdditionalAxis1 : null
                    visible: fact && fact.userVisible
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: additionalAxesManualControl.checked ? qsTr("Aux2") : qsTr("Channel 6")
                    fact: _joystickSettings ? _joystickSettings.enableAdditionalAxis2 : null
                    visible: fact && fact.userVisible
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: additionalAxesManualControl.checked ? qsTr("Aux3") : qsTr("Channel 7")
                    fact: _joystickSettings ? _joystickSettings.enableAdditionalAxis3 : null
                    visible: fact && fact.userVisible
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: additionalAxesManualControl.checked ? qsTr("Aux4") : qsTr("Channel 8")
                    fact: _joystickSettings ? _joystickSettings.enableAdditionalAxis4 : null
                    visible: fact && fact.userVisible
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: additionalAxesManualControl.checked ? qsTr("Aux5") : qsTr("Channel 9")
                    fact: _joystickSettings ? _joystickSettings.enableAdditionalAxis5 : null
                    visible: fact && fact.userVisible
                }

                FactCheckBoxSlider {
                    Layout.fillWidth: true
                    text: additionalAxesManualControl.checked ? qsTr("Aux6") : qsTr("Channel 10")
                    fact: _joystickSettings ? _joystickSettings.enableAdditionalAxis6 : null
                    visible: fact && fact.userVisible
                }
            }
        }
    }
}
