pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.VehicleSetup
import QGroundControl.FactControls

ColumnLayout {
    id: root
    spacing: ScreenTools.defaultFontPixelHeight / 2

    required property var joystick
    required property var controller

    readonly property var _vehicle: controller.vehicle
    readonly property bool _firmwareButtonsSupported: _vehicle ? _vehicle.supports.jsButton : false
    readonly property bool _firmwareButtonsReady: _firmwareButtonsSupported && _vehicle.parameterManager.parametersReady
    readonly property real _numberWidth: ScreenTools.defaultFontPixelWidth * 6
    readonly property real _repeatWidth: ScreenTools.defaultFontPixelWidth * 10
    readonly property real _columnSpacing: ScreenTools.defaultFontPixelWidth
    readonly property real _actionWidth: Math.max(ScreenTools.defaultFontPixelWidth * 18,
        (width - _numberWidth - _repeatWidth - _columnSpacing * (_firmwareButtonsSupported ? 6 : 4))
        / (_firmwareButtonsSupported ? 3 : 1))

    QGCPalette { id: qgcPal }

    function actionIndex(actionName) {
        if (!joystick) return -1
        for (let i = 0; i < joystick.assignableActionTitles.length; i++) {
            if (joystick.assignableActions.get(i).action === actionName) return i
        }
        return -1
    }

    QGCLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Press a button to highlight its row. Choose a ground station action or a vehicle action.")
    }

    QGCLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Multiple buttons that have the same action must be pressed simultaneously to invoke the action.")
    }

    RowLayout {
        spacing: root._columnSpacing
        QGCLabel { Layout.preferredWidth: root._numberWidth; text: qsTr("Button") }
        QGCLabel { Layout.preferredWidth: root._actionWidth; text: qsTr("Ground station action") }
        QGCLabel { Layout.preferredWidth: root._actionWidth; visible: root._firmwareButtonsSupported; text: qsTr("Vehicle action") }
        QGCLabel { Layout.preferredWidth: root._actionWidth; visible: root._firmwareButtonsSupported; text: qsTr("Shift action") }
        QGCLabel { Layout.preferredWidth: root._repeatWidth; text: qsTr("Repeat") }
    }

    Connections {
        target: root.joystick
        function onRawButtonPressedChanged(index, pressed) {
            const row = buttonRepeater.itemAt(index)
            if (row) row.pressed = pressed
        }
    }

    Repeater {
        id: buttonRepeater
        model: root.joystick ? Math.min(root.joystick.buttonCount, 64) : 0

        Rectangle {
            id: buttonRow
            required property int index
            objectName: "joystickButtonRow" + index
            Layout.fillWidth: true
            implicitHeight: assignmentLayout.implicitHeight + ScreenTools.defaultFontPixelHeight / 2
            implicitWidth: assignmentLayout.implicitWidth
            color: pressed ? qgcPal.buttonHighlight : (index % 2 ? qgcPal.windowShade : qgcPal.window)
            radius: ScreenTools.defaultBorderRadius

            property bool pressed: false
            readonly property string _functionName: "BTN" + index + "_FUNCTION"
            readonly property string _shiftFunctionName: "BTN" + index + "_SFUNCTION"
            property Fact _buttonFunction: root._firmwareButtonsReady && root.controller.parameterExists(-1, _functionName)
                                           ? root.controller.getParameterFact(-1, _functionName, false) : null
            property Fact _shiftFunction: root._firmwareButtonsReady && root.controller.parameterExists(-1, _shiftFunctionName)
                                          ? root.controller.getParameterFact(-1, _shiftFunctionName, false) : null

            RowLayout {
                id: assignmentLayout
                anchors.verticalCenter: parent.verticalCenter
                spacing: root._columnSpacing

                QGCLabel {
                    Layout.preferredWidth: root._numberWidth
                    horizontalAlignment: Text.AlignHCenter
                    text: buttonRow.index
                    color: buttonRow.pressed ? qgcPal.buttonHighlightText : qgcPal.text
                }

                QGCComboBox {
                    id: buttonActionCombo
                    objectName: "joystickQgcActionCombo" + buttonRow.index
                    Layout.preferredWidth: root._actionWidth
                    model: root.joystick ? root.joystick.assignableActionTitles : []

                    onActivated: (index) => {
                        const action = root.joystick.assignableActions.get(index)
                        if (!action) return
                        if (buttonRow._buttonFunction) buttonRow._buttonFunction.rawValue = 0
                        if (buttonRow._shiftFunction) buttonRow._shiftFunction.rawValue = 0
                        root.joystick.setButtonAction(buttonRow.index, action.action)
                    }

                    function refreshAction() {
                        if (!root.joystick) return
                        const action = root.joystick.buttonActions[buttonRow.index]
                        currentIndex = root.actionIndex(action)
                        alternateText = currentIndex < 0 ? action : ""
                    }

                    Component.onCompleted: refreshAction()
                    Connections {
                        target: root.joystick
                        function onButtonActionsChanged() { buttonActionCombo.refreshAction() }
                        function onAssignableActionsChanged() { buttonActionCombo.refreshAction() }
                    }
                }

                FactComboBox {
                    objectName: "joystickFirmwareActionCombo" + buttonRow.index
                    Layout.preferredWidth: root._actionWidth
                    visible: root._firmwareButtonsSupported
                    enabled: !!buttonRow._buttonFunction
                    fact: buttonRow._buttonFunction
                    indexModel: false
                    alternateText: !fact ? (root._firmwareButtonsReady ? qsTr("Unavailable") : qsTr("Waiting for parameters")) : ""
                    onActivated: (index) => {
                        if (buttonRow._buttonFunction) buttonRow._buttonFunction.enumIndex = index
                        if (buttonRow._buttonFunction && buttonRow._buttonFunction.rawValue !== 0) {
                            root.joystick.setButtonAction(buttonRow.index, root.joystick.buttonActionNone)
                            buttonActionCombo.refreshAction()
                        }
                    }
                }

                FactComboBox {
                    objectName: "joystickShiftActionCombo" + buttonRow.index
                    Layout.preferredWidth: root._actionWidth
                    visible: root._firmwareButtonsSupported
                    enabled: !!buttonRow._shiftFunction
                    fact: buttonRow._shiftFunction
                    indexModel: false
                    alternateText: !fact ? (root._firmwareButtonsReady ? qsTr("Unavailable") : qsTr("Waiting for parameters")) : ""
                    onActivated: (index) => {
                        if (buttonRow._shiftFunction) buttonRow._shiftFunction.enumIndex = index
                        if (buttonRow._shiftFunction && buttonRow._shiftFunction.rawValue !== 0) {
                            root.joystick.setButtonAction(buttonRow.index, root.joystick.buttonActionNone)
                            buttonActionCombo.refreshAction()
                        }
                    }
                }

                QGCCheckBox {
                    id: repeatCheckBox
                    Layout.preferredWidth: root._repeatWidth
                    enabled: buttonActionCombo.currentIndex >= 0
                             && !!root.joystick.assignableActions.get(buttonActionCombo.currentIndex)
                             && root.joystick.assignableActions.get(buttonActionCombo.currentIndex).canRepeat
                    onClicked: root.joystick.setButtonRepeat(buttonRow.index, checked)
                    function refreshRepeat() { checked = root.joystick ? root.joystick.getButtonRepeat(buttonRow.index) : false }
                    Component.onCompleted: refreshRepeat()
                    Connections {
                        target: root.joystick
                        function onButtonActionsChanged() { repeatCheckBox.refreshRepeat() }
                    }
                }
            }
        }
    }
}
