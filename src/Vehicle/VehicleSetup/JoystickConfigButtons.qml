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
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Palette
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.Controllers
import QGroundControl.FactSystem
import QGroundControl.FactControls

ColumnLayout {
    width:                  availableWidth
    height:                 (globals.activeVehicle.supportsJSButton ? buttonCol.height : flowColumn.height) + (ScreenTools.defaultFontPixelHeight * 2)
    spacing:                ScreenTools.defaultFontPixelHeight
    
    Connections {
        target: _activeJoystick
        onRawButtonPressedChanged: (index, pressed) => {
            if (buttonActionRepeater.itemAt(index)) {
                buttonActionRepeater.itemAt(index).pressed = pressed
            }
            if (jsButtonActionRepeater.itemAt(index)) {
                jsButtonActionRepeater.itemAt(index).pressed = pressed
            }
        }
    }

    ColumnLayout {
        id:         flowColumn
        width:      parent.width
        spacing:    ScreenTools.defaultFontPixelHeight

        // Note for reminding the use of multiple buttons for the same action
        QGCLabel {
            Layout.preferredWidth:  parent.width
            wrapMode:               Text.WordWrap
            text:                   qsTr(" Multiple buttons that have the same action must be pressed simultaneously to invoke the action.")
        }
        
        Flow {
            id:                     buttonFlow
            Layout.preferredWidth:  parent.width
            spacing:                ScreenTools.defaultFontPixelWidth
            visible:                !globals.activeVehicle.supportsJSButton
            Repeater {
                id:             buttonActionRepeater
                model:          _activeJoystick ? Math.min(_activeJoystick.totalButtonCount, _maxButtons) : []
                Row {
                    spacing:    ScreenTools.defaultFontPixelWidth
                    property bool pressed
                    property var  currentAssignableAction: _activeJoystick ? _activeJoystick.assignableActions.get(buttonActionCombo.currentIndex) : null
                    Rectangle {
                        anchors.verticalCenter:     parent.verticalCenter
                        width:                      ScreenTools.defaultFontPixelHeight * 1.5
                        height:                     width
                        border.width:               1
                        border.color:               qgcPal.text
                        color:                      pressed ? qgcPal.buttonHighlight : qgcPal.button
                        QGCLabel {
                            anchors.fill:           parent
                            color:                  pressed ? qgcPal.buttonHighlightText : qgcPal.buttonText
                            horizontalAlignment:    Text.AlignHCenter
                            verticalAlignment:      Text.AlignVCenter
                            text:                   modelData
                        }
                    }
                    QGCComboBox {
                        id:                         buttonActionCombo
                        width:                      ScreenTools.defaultFontPixelWidth * 26
                        model:                      _activeJoystick ? _activeJoystick.assignableActionTitles : []
                        sizeToContents:             true

                        function _findCurrentButtonAction() {
                            if(_activeJoystick) {
                                var i = find(_activeJoystick.buttonActions[modelData])
                                if(i < 0) i = 0
                                currentIndex = i
                            }
                        }

                        Component.onCompleted:  _findCurrentButtonAction()
                        onModelChanged:         _findCurrentButtonAction()
                        onActivated: (index) => { _activeJoystick.setButtonAction(modelData, textAt(index)) }
                    }
                    QGCCheckBox {
                        id:                         repeatCheck
                        text:                       qsTr("Repeat")
                        enabled:                    currentAssignableAction && _activeJoystick.calibrated && currentAssignableAction.canRepeat
                        onClicked: {
                            _activeJoystick.setButtonRepeat(modelData, checked)
                        }
                        Component.onCompleted: {
                            if(_activeJoystick) {
                                checked = _activeJoystick.getButtonRepeat(modelData)
                            }
                        }
                        anchors.verticalCenter:     parent.verticalCenter
                    }
                    Item {
                        width:                      ScreenTools.defaultFontPixelWidth * 2
                        height:                     1
                    }
                }
            }
        }
    }
    Column {
        id:         buttonCol
        width:      parent.width
        visible:    globals.activeVehicle.supportsJSButton
        spacing:    ScreenTools.defaultFontPixelHeight / 3
        Row {
            spacing: ScreenTools.defaultFontPixelWidth
            QGCLabel {
                horizontalAlignment:    Text.AlignHCenter
                width:                  ScreenTools.defaultFontPixelHeight * 1.5
                text:                   qsTr("#")
            }
            QGCLabel {
                width:                  ScreenTools.defaultFontPixelWidth * 26
                text:                   qsTr("Function: ")
            }
            QGCLabel {
                width:                  ScreenTools.defaultFontPixelWidth * 26
                visible:                globals.activeVehicle.supportsJSButton
                text:                   qsTr("Shift Function: ")
            }
        }
        Repeater {
            id:     jsButtonActionRepeater
            model:  _activeJoystick ? Math.min(_activeJoystick.totalButtonCount, _maxButtons) : 0

            Row {
                spacing: ScreenTools.defaultFontPixelWidth
                visible: globals.activeVehicle.supportsJSButton
                property int buttonIndex: index
                property var parameterName: `BTN${buttonIndex}_FUNCTION`
                property var parameterShiftName: `BTN${buttonIndex}_SFUNCTION`
                // parameterExists() is not a reactive QML call. Make the Fact
                // lookup depend on parametersReady so rows created while the
                // initial parameter download is running refresh afterwards.
                property bool parametersReady: controller.vehicle && controller.vehicle.parameterManager.parametersReady
                property bool hasFirmwareSupport: parametersReady && controller.parameterExists(-1, parameterName)

                property bool pressed
                property var  currentAssignableAction: _activeJoystick ? _activeJoystick.assignableActions.get(buttonActionCombo.currentIndex) : null

                Rectangle {
                    anchors.verticalCenter:     parent.verticalCenter
                    width:                      ScreenTools.defaultFontPixelHeight * 1.5
                    height:                     width
                    border.width:               1
                    border.color:               qgcPal.text
                    color:                      pressed ? qgcPal.buttonHighlight : qgcPal.button


                    QGCLabel {
                        anchors.fill:           parent
                        color:                  pressed ? qgcPal.buttonHighlightText : qgcPal.buttonText
                        horizontalAlignment:    Text.AlignHCenter
                        verticalAlignment:      Text.AlignVCenter
                        text:                   modelData
                    }
                }

                QGCComboBox {
                    id:                         buttonActionCombo
                    width:                      ScreenTools.defaultFontPixelWidth * 26
                    property Fact fact:         parametersReady && controller.parameterExists(-1, parameterName) ? controller.getParameterFact(-1, parameterName) : null
                    property Fact fact_shift:   parametersReady && controller.parameterExists(-1, parameterShiftName) ? controller.getParameterFact(-1, parameterShiftName) : null
                    property var factOptions:   fact ? fact.enumStrings : []
                    property var qgcActions:    _activeJoystick.assignableActionTitles.filter(
                        function(s) {
                            return [
                                s.includes("Camera")
                                , s.includes("Stream")
                                , s.includes("Stream")
                                , s.includes("Zoom")
                                , s.includes("Gimbal")
                                , s.includes("No Action")
                            ].some(Boolean)
                        }
                    )

                    model:                      [...qgcActions, ...factOptions]
                    property bool isFwAction:   currentIndex >= qgcActions.length && currentIndex < model.length
                    sizeToContents: true

                    function _findCurrentButtonAction() {
                        if (!_activeJoystick) {
                            currentIndex = 0
                            return
                        }

                        // Fact metadata can arrive after this delegate is created. Defer the
                        // lookup so the combined QGC/firmware model is complete before setting
                        // currentIndex. Otherwise ComboBox clamps an out-of-range firmware
                        // index to the final QGC action, which makes unrelated buttons appear
                        // as Gimbal Yaw Follow.
                        Qt.callLater(function() {
                            if (fact && fact.value > 0 && factOptions.length > 0) {
                                const firmwareIndex = fact.enumIndex
                                if (firmwareIndex >= 0 && firmwareIndex < factOptions.length) {
                                    currentIndex = qgcActions.length + firmwareIndex
                                    // Firmware and QGC must not handle the same button.
                                    _activeJoystick.setButtonAction(buttonIndex, "No Action")
                                    return
                                }
                            }

                            const qgcIndex = qgcActions.indexOf(_activeJoystick.buttonActions[buttonIndex])
                            currentIndex = qgcIndex >= 0 ? qgcIndex : 0
                        })
                    }

                    Component.onCompleted: {
                        _findCurrentButtonAction()
                    }
                    onModelChanged:         _findCurrentButtonAction()
                    onFactChanged:          _findCurrentButtonAction()
                    onActivated:            function (optionIndex) {
                        if (optionIndex >= qgcActions.length) {
                            // This is a FW action, set parameter to the action and set QGC's handler to No Action
                            const firmwareIndex = optionIndex - qgcActions.length
                            if (fact && firmwareIndex >= 0 && firmwareIndex < fact.enumValues.length) {
                                fact.value = fact.enumValues[firmwareIndex]
                                _activeJoystick.setButtonAction(buttonIndex, "No Action")
                            }
                        } else {
                            // This is a QGC action, set parameters to Disabled and QGC to the desired action
                            const func = textAt(optionIndex)
                            _activeJoystick.setButtonAction(buttonIndex, func)
                            if (fact) {
                                fact.value = 0
                            }
                            if (fact_shift) {
                                fact_shift.value = 0
                            }
                        }
                    }

                    Connections {
                        target: buttonActionCombo.fact

                        function onEnumsChanged() {
                            buttonActionCombo._findCurrentButtonAction()
                        }

                        function onValueChanged() {
                            buttonActionCombo._findCurrentButtonAction()
                        }
                    }
                }
                QGCCheckBox {
                    id:                         repeatCheck
                    text:                       qsTr("Repeat")
                    enabled:                    currentAssignableAction && _activeJoystick.calibrated && currentAssignableAction.canRepeat
                    visible:                    !globals.activeVehicle.supportsJSButton

                    onClicked: {
                        _activeJoystick.setButtonRepeat(modelData, checked)
                    }
                    Component.onCompleted: {
                        if(_activeJoystick) {
                            checked = _activeJoystick.getButtonRepeat(modelData)
                        }
                    }
                    anchors.verticalCenter:     parent.verticalCenter
                }
                Item {
                    width:                      ScreenTools.defaultFontPixelWidth * 2
                    height:                     1
                }

                FactComboBox {
                    id:         shiftJSButtonActionCombo
                    width:      ScreenTools.defaultFontPixelWidth * 26
                    fact:       buttonActionCombo.fact_shift
                    indexModel: false
                    visible:    buttonActionCombo.isFwAction && fact
                    sizeToContents: true
                }

                QGCLabel {
                    text:                   qsTr("QGC functions do not support shift actions")
                    width:                  ScreenTools.defaultFontPixelWidth * 15
                    visible:                hasFirmwareSupport && !buttonActionCombo.isFwAction
                    anchors.verticalCenter: parent.verticalCenter
                }
                QGCLabel {
                    text:                   qsTr("No firmware support")
                    width:                  ScreenTools.defaultFontPixelWidth * 15
                    visible:                !hasFirmwareSupport
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}


