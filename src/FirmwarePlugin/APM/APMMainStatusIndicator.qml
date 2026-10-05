import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls

ColumnLayout {
    spacing: ScreenTools.defaultFontPixelHeight / 2

    FactPanelController { id: controller }

    property var _gcsEnableFact:  controller.getParameterFact(-1, "FS_GCS_ENABLE", false)
    property var _gcsTimeoutFact: controller.getParameterFact(-1, "FS_GCS_TIMEOUT", false)
    property var _failsafeOptionsFact: controller.getParameterFact(-1, "FS_OPTIONS", false)

    SettingsGroupLayout {
        heading:            qsTr("Ground Control Comm Loss Failsafe")
        Layout.fillWidth:   true
        visible:            _gcsEnableFact || _gcsTimeoutFact

        LabelledFactComboBox {
            label:      qsTr("Vehicle Action")
            fact:       _gcsEnableFact
            indexModel: false
            visible:    _gcsEnableFact
        }

        FactSlider {
            Layout.fillWidth:       true
            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 20
            label:                  qsTr("Loss Timeout")
            fact:                   _gcsTimeoutFact
            majorTickStepSize:      5
            visible:                _gcsTimeoutFact
        }
    }

    SettingsGroupLayout {
        heading:            qsTr("Failsafe Options")
        Layout.fillWidth:   true
        visible:            _failsafeOptionsFact

        Repeater {
            id:     repeater
            model:  fact ? fact.bitmaskStrings : []

            property Fact fact: _failsafeOptionsFact

            QGCCheckBoxSlider {
                Layout.fillWidth: true
                text:               modelData
                checked:            fact.value & fact.bitmaskValues[index]

                property Fact fact: repeater.fact

                onClicked: {
                    var i
                    var otherCheckbox
                    if (checked) {
                        fact.value |= fact.bitmaskValues[index]
                    } else {
                        fact.value &= ~fact.bitmaskValues[index]
                    }
                }
            }
        }
    }
}
