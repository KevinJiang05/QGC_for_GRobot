import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.FactControls
import QGroundControl.Controls
import QGroundControl.AutoPilotPlugins.APM

ColumnLayout {
    property int instance: 1
    property alias parameterController: gimbalParams.controller
    property real verticalSpacing: ScreenTools.defaultFontPixelHeight / 2
    property real horizontalSpacing: ScreenTools.defaultFontPixelWidth * 2

    id: control

    property real _sliderWidth: ScreenTools.defaultFontPixelWidth * 20
    property var _controller: gimbalParams.controller
    property var _rcRateFact: gimbalParams.rcRateFact

    function _servoParameterName(channel, suffix) {
        const servoName = "SERVO" + channel + "_" + suffix
        const rcName = "RC" + channel + "_" + suffix
        return _controller.parameterExists(-1, servoName) || !gimbalParams.legacyParameters ? servoName : rcName
    }

    function _servoFact(channel, suffix) {
        return _controller.getParameterFact(-1, _servoParameterName(channel, suffix), false)
    }

    function _servoChannelCount() {
        if (gimbalParams.legacyParameters) {
            let lastChannel = 0
            for (let channel = 1; channel <= 16; channel++) {
                if (_controller.parameterExists(-1, _servoParameterName(channel, "FUNCTION"))) {
                    lastChannel = channel
                }
            }
            return lastChannel
        }
        let servoIndex = 1
        while (_controller.parameterExists(-1, "SERVO" + servoIndex + "_FUNCTION")) {
            servoIndex++
        }
        return servoIndex - 1
    }

    function _rcChannelCount() {
        let rcIndex = 1
        while (_controller.parameterExists(-1, "RC" + rcIndex + "_OPTION")) {
            rcIndex++
        }
        return rcIndex - 1
    }

    function _servoChannelModel() {
        let model = [ qsTr("Disabled") ]
        let channelCount = _servoChannelCount()
        for (let i = 1; i <= channelCount; i++) {
            model.push(qsTr("Channel ") + i)
        }
        return model
    }

    function _rcChannelModel() {
        let model = [ qsTr("Disabled") ]
        let channelCount = _rcChannelCount()
        for (let i = 1; i <= channelCount; i++) {
            model.push(qsTr("Channel ") + i)
        }
        return model
    }

    APMGimbalParams { id: gimbalParams; instance: control.instance }

    RowLayout {
        Layout.fillWidth: false
        spacing: horizontalSpacing
        visible: gimbalParams.instanceCount > 0

        LabelledFactComboBox {
            label: qsTr("Gimbal Type")
            fact: gimbalParams.typeFact
            visible: fact !== null
            indexModel: false
            comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 30
        }

        LabelledFactComboBox {
            label: qsTr("Default Mode")
            fact: gimbalParams.defaultModeFact
            indexModel: false
            comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 30
            visible: fact !== null
        }
    }

    Loader {
        sourceComponent: gimbalParams.paramsAvailable ? configComponent : rebootRequiredComponent
    }

    Component {
        id: configComponent

        ColumnLayout {
            spacing: verticalSpacing

            SettingsGroupLayout {
                heading: qsTr("Neutral Position")
                visible: gimbalParams.neutralXFact !== null || gimbalParams.neutralYFact !== null || gimbalParams.neutralZFact !== null

                RowLayout {
                    spacing: horizontalSpacing

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("Pitch")
                        fact: gimbalParams.neutralYFact
                        visible: fact !== null
                    }

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("Yaw")
                        fact: gimbalParams.neutralZFact
                        visible: fact !== null
                    }

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("Roll")
                        fact: gimbalParams.neutralXFact
                        visible: fact !== null
                    }
                }
            }

            SettingsGroupLayout {
                heading: qsTr("Retracted Position")
                visible: gimbalParams.retractXFact !== null || gimbalParams.retractYFact !== null || gimbalParams.retractZFact !== null

                RowLayout {
                    spacing: horizontalSpacing

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("Pitch")
                        fact: gimbalParams.retractYFact
                        visible: fact !== null
                    }

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("Yaw")
                        fact: gimbalParams.retractZFact
                        visible: fact !== null
                    }

                    LabelledFactTextField {
                        Layout.fillWidth: true
                        label: qsTr("Roll")
                        fact: gimbalParams.retractXFact
                        visible: fact !== null
                    }
                }
            }

            SettingsGroupLayout {
                heading: qsTr("Axis Constraints")

                Repeater {
                    model: [
                        {
                            axisLabel: qsTr("Pitch"),
                            minFact: gimbalParams.pitchMinFact,
                            maxFact: gimbalParams.pitchMaxFact
                        },
                        {
                            axisLabel: qsTr("Yaw"),
                            minFact: gimbalParams.yawMinFact,
                            maxFact: gimbalParams.yawMaxFact
                        },
                        {
                            axisLabel: qsTr("Roll"),
                            minFact: gimbalParams.rollMinFact,
                            maxFact: gimbalParams.rollMaxFact
                        }
                    ]

                    ColumnLayout {
                        spacing: verticalSpacing
                        visible: modelData.minFact !== null || modelData.maxFact !== null

                        QGCLabel {
                            text: modelData.axisLabel
                        }

                        RowLayout {
                            spacing: horizontalSpacing

                            LabelledFactTextField {
                                Layout.fillWidth: true
                                label: qsTr("Min Angle")
                                fact: modelData.minFact
                                visible: fact !== null
                            }

                            LabelledFactTextField {
                                Layout.fillWidth: true
                                label: qsTr("Max Angle")
                                fact: modelData.maxFact
                                visible: fact !== null
                            }
                        }
                    }
                }
            }

            SettingsGroupLayout {
                heading: qsTr("RC Targetting")

                RowLayout {
                    spacing: horizontalSpacing
                    visible: gimbalParams.legacyParameters
                    Repeater {
                        model: [
                            { axisLabel: qsTr("Pitch"), inputFact: gimbalParams.pitchInputFact },
                            { axisLabel: qsTr("Yaw"), inputFact: gimbalParams.yawInputFact },
                            { axisLabel: qsTr("Roll"), inputFact: gimbalParams.rollInputFact }
                        ]
                        LabelledFactComboBox {
                            label: modelData.axisLabel
                            fact: modelData.inputFact
                            indexModel: false
                            visible: fact !== null
                            Connections {
                                target: modelData.inputFact
                                function onValueChanged() {
                                    if (gimbalParams.defaultModeFact) {
                                        gimbalParams.defaultModeFact.rawValue = 3
                                    }
                                }
                            }
                        }
                    }
                }

                LabelledFactTextField {
                    label: qsTr("Joystick speed")
                    fact: gimbalParams.joystickSpeedFact
                    visible: fact !== null
                }

                RowLayout {
                    spacing: horizontalSpacing
                    visible: !gimbalParams.legacyParameters

                    Repeater {
                        model: [
                            { comboLabel: qsTr("Pitch"), optionValue: 213 },
                            { comboLabel: qsTr("Yaw"), optionValue: 214 },
                            { comboLabel: qsTr("Roll"), optionValue: 212 }
                        ]

                        LabelledComboBox {
                            id: outputChannelCombo
                            label: modelData.comboLabel
                            comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 30
                            model: _rcChannelModel()

                            Component.onCompleted: {
                                let maxRcChannel = _rcChannelCount()
                                for (let rcIndex = 1; rcIndex <= maxRcChannel; rcIndex++) {
                                    let parameterName = "RC" + rcIndex + "_OPTION"
                                    let functionFact = _controller.getParameterFact(-1, parameterName)
                                    if (functionFact.value == modelData.optionValue) {
                                        currentIndex = rcIndex
                                        return
                                    }
                                }
                                currentIndex = 0
                            }

                            onActivated: {
                                if (currentIndex == 0) {
                                    // Disabled selected
                                    let maxRcChannel = _rcChannelCount()
                                    for (let rcIndex = 1; rcIndex <= maxRcChannel; rcIndex++) {
                                        let parameterName = "RC" + rcIndex + "_OPTION"
                                        let functionFact = _controller.getParameterFact(-1, parameterName)
                                        if (functionFact.value == modelData.optionValue) {
                                            functionFact.rawValue = 0
                                            return
                                        }
                                    }
                                    return
                                }
                                let rcIndex = currentIndex
                                let parameterName = "RC" + rcIndex + "_OPTION"
                                _controller.getParameterFact(-1, parameterName).rawValue = modelData.optionValue
                            }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 0
                    visible: _rcRateFact !== null

                    RowLayout {
                        spacing: horizontalSpacing

                        QGCRadioButton {
                            text: qsTr("Angle Control")
                            checked: _rcRateFact ? !(_rcRateFact.rawValue > 0) : false
                            onClicked: _rcRateFact.rawValue = 0
                        }

                        QGCRadioButton {
                            text: qsTr("Rate Control")
                            checked: _rcRateFact ? _rcRateFact.rawValue > 0 : false
                            onClicked: _rcRateFact.rawValue = 90
                        }

                        LabelledFactTextField {
                            Layout.fillWidth: true
                            label: qsTr("Rate")
                            fact: _rcRateFact
                        }
                    }
                }
            }

            SettingsGroupLayout {
                heading: qsTr("Servo Controlled Gimbal")
                visible: gimbalParams.typeFact ? gimbalParams.typeFact.rawValue === 1 : gimbalParams.legacyParameters

                Repeater {
                    model: [
                        { axisLabel: qsTr("Pitch"), functionValue: 7, stabilizeFact: gimbalParams.pitchLeadFact, legacyStabilizeFact: gimbalParams.pitchStabilizeFact },
                        { axisLabel: qsTr("Yaw"), functionValue: 6, stabilizeFact: null, legacyStabilizeFact: gimbalParams.yawStabilizeFact },
                        { axisLabel: qsTr("Roll"), functionValue: 8, stabilizeFact: gimbalParams.rollLeadFact, legacyStabilizeFact: gimbalParams.rollStabilizeFact }
                    ]

                    ColumnLayout {
                        spacing: verticalSpacing

                        property bool servoChannelValid: outputChannelCombo.currentIndex > 0
                        property int validServoChannel: servoChannelValid ? outputChannelCombo.currentIndex : 1
                        property string servoPrefix: "SERVO" + validServoChannel + "_"
                        property bool hasStabilizeParam: modelData.stabilizeFact !== null

                        RowLayout {
                            Layout.fillWidth: false
                            spacing: horizontalSpacing

                            QGCLabel {
                                text: modelData.axisLabel
                            }

                            FactCheckBox {
                                text: qsTr("Servo Reversed")
                                fact: _servoFact(validServoChannel, "REVERSED")
                                visible: fact !== null
                                enabled: servoChannelValid
                            }

                            FactCheckBox {
                                text: qsTr("Stabilize")
                                fact: modelData.legacyStabilizeFact
                                visible: fact !== null
                                enabled: servoChannelValid
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: hasStabilizeParam
                            spacing: horizontalSpacing

                            LabelledComboBox {
                                Layout.fillWidth: hasStabilizeParam
                                id: outputChannelCombo
                                objectName: "gimbalOutputChannel_" + modelData.functionValue
                                label: qsTr("Output Channel")
                                comboBoxPreferredWidth: ScreenTools.defaultFontPixelWidth * 30
                                model: _servoChannelModel()

                                Component.onCompleted: {
                                    let maxServoChannel = _servoChannelCount()
                                    for (let servoIndex = 1; servoIndex <= maxServoChannel; servoIndex++) {
                                        let functionFact = _servoFact(servoIndex, "FUNCTION")
                                        if (functionFact && functionFact.value == modelData.functionValue) {
                                            currentIndex = servoIndex
                                            return
                                        }
                                    }
                                    currentIndex = 0
                                }

                                onActivated: (index) => {
                                    const selectedFact = index > 0 ? _servoFact(index, "FUNCTION") : null
                                    if (index > 0 && !selectedFact) {
                                        return
                                    }
                                    // Keep this axis assigned to one output, including when disabling stale duplicates.
                                    const maxServoChannel = _servoChannelCount()
                                    for (let servoIndex = 1; servoIndex <= maxServoChannel; servoIndex++) {
                                        const functionFact = _servoFact(servoIndex, "FUNCTION")
                                        if (servoIndex !== index && functionFact && functionFact.value == modelData.functionValue) {
                                            functionFact.rawValue = 0
                                        }
                                    }
                                    if (selectedFact && selectedFact.value != modelData.functionValue) {
                                        selectedFact.rawValue = modelData.functionValue
                                    }
                                }
                            }

                            LabelledFactTextField {
                                Layout.fillWidth: true
                                label: qsTr("Stabilization Lead")
                                fact: modelData.stabilizeFact
                                visible: hasStabilizeParam
                                enabled: servoChannelValid
                            }
                        }

                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            spacing: horizontalSpacing
                            enabled: servoChannelValid

                            LabelledFactTextField {
                                Layout.fillWidth: true
                                label: qsTr("Min PWM")
                                fact: _servoFact(validServoChannel, "MIN")
                                visible: fact !== null
                            }

                            LabelledFactTextField {
                                Layout.fillWidth: true
                                label: qsTr("Max PWM")
                                fact: _servoFact(validServoChannel, "MAX")
                                visible: fact !== null
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: rebootRequiredComponent

        QGCLabel {
            text: qsTr("Gimbal settings will be available after rebooting the vehicle.")
            visible: gimbalParams.typeFact !== null && gimbalParams.typeFact.rawValue !== 0
        }
    }
}
