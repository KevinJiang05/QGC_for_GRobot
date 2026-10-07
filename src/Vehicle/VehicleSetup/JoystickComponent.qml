import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.VehicleSetup
import QGroundControl.FactControls

SetupPage {
    id: setupPage
    objectName: "joystickSetupPage"
    pageComponent: joystickPageComponent

    Component {
        id: joystickPageComponent

        ColumnLayout {
            id: root
            width: setupPage.availableWidth
            spacing: ScreenTools.defaultFontPixelHeight

            readonly property Fact activeJoystickNameFact: QGroundControl.settingsManager.joystickManagerSettings.activeJoystickName
            readonly property string activeJoystickName: activeJoystickNameFact.value
            readonly property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
            readonly property var activeJoystick: joystickManager.activeJoystick
            readonly property bool activeJoystickCalibrated: activeJoystick ? activeJoystick.settings.calibrated.rawValue : false
            readonly property bool configurationAvailable: activeJoystick && activeVehicle
                                                           && (!activeVehicle.armed || activeVehicle.setupSafetyRestrictionsDisabled)

            function reloadConfiguration() {
                const configuration = configurationLoader.item
                if (!configuration || (configuration.joystickDevice === root.activeJoystick
                                       && configuration.configurationVehicle === root.activeVehicle)) return
                // A controller is bound to one joystick and vehicle for its lifetime.
                configurationLoader.active = false
                Qt.callLater(function() {
                    configurationLoader.active = Qt.binding(function() { return root.configurationAvailable })
                })
            }

            onActiveJoystickChanged: reloadConfiguration()
            onActiveVehicleChanged: reloadConfiguration()

            RowLayout {
                Layout.fillWidth: true
                spacing: ScreenTools.defaultFontPixelWidth

                QGCLabel { text: qsTr("Joystick:") }

                QGCComboBox {
                    id: joystickCombo
                    objectName: "joystickDeviceCombo"
                    Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 32
                    enabled: QGroundControl.corePlugin.options.allowJoystickSelection
                             && (!configurationLoader.item || !configurationLoader.item.calibrating)
                    onActivated: (index) => { root.activeJoystickNameFact.rawValue = textAt(index) }

                    function recalc() {
                        const names = [...(joystickManager.availableJoystickNames || [])]
                        if (root.activeJoystickName && !names.includes(root.activeJoystickName)) {
                            names.push(root.activeJoystickName)
                        }
                        model = names
                        currentIndex = find(root.activeJoystickName)
                    }

                    Component.onCompleted: recalc()
                    Connections { target: root; function onActiveJoystickNameChanged() { joystickCombo.recalc() } }
                    Connections { target: joystickManager; function onAvailableJoystickNamesChanged() { joystickCombo.recalc() } }
                }

                QGCCheckBox {
                    objectName: "joystickEnableCheckBox"
                    text: qsTr("Enable joystick control")
                    checked: joystickManager.activeJoystickEnabledForActiveVehicle
                    enabled: root.activeJoystickCalibrated && !!root.activeVehicle
                             && (!configurationLoader.item || !configurationLoader.item.calibrating)
                    onClicked: joystickManager.activeJoystickEnabledForActiveVehicle = checked
                }

                QGCLabel {
                    objectName: "joystickCalibrationStatus"
                    Layout.fillWidth: true
                    text: !root.activeJoystick ? qsTr("Not currently available")
                          : (root.activeJoystick.axisCount === 0 ? qsTr("Buttons only")
                             : (root.activeJoystickCalibrated ? qsTr("Calibrated") : qsTr("Requires Calibration")))
                    color: root.activeJoystickCalibrated ? qgcPal.text : qgcPal.warningText
                }
            }

            QGCLabel {
                Layout.fillWidth: true
                visible: !root.configurationAvailable
                wrapMode: Text.WordWrap
                text: !root.activeJoystick ? qsTr("No joysticks or gamepads detected. Connect a device to configure it.")
                      : (!root.activeVehicle ? qsTr("Connect a vehicle to configure joystick control.")
                         : qsTr("Disarm the vehicle before configuring the joystick."))
            }

            Loader {
                id: configurationLoader
                Layout.fillWidth: true
                objectName: "joystickConfigurationLoader"
                active: root.configurationAvailable
                sourceComponent: configurationComponent
            }

            Component {
                id: configurationComponent

                ColumnLayout {
                    id: configuration
                    spacing: ScreenTools.defaultFontPixelHeight
                    readonly property bool calibrating: joystickController.calibrating
                    readonly property var configurationVehicle: joystickController.vehicle
                    property var joystickDevice: root.activeJoystick

                    Component.onCompleted: {
                        joystickDevice = root.activeJoystick
                        joystickController.start()
                        tabBar.currentIndex = configuration.joystickDevice.axisCount === 0 ? 1
                                              : (root.activeJoystickCalibrated ? 0 : 2)
                    }

                    Connections {
                        target: joystickController
                        function onCalibrationCompleted() {
                            if (joystickManager.activeJoystickEnabledForActiveVehicle) return
                            QGroundControl.showMessageDialog(
                                root, qsTr("Enable Joystick"),
                                qsTr("%1 calibration is complete. Enable it now?").arg(configuration.joystickDevice.name),
                                Dialog.Yes | Dialog.No,
                                function() { joystickManager.activeJoystickEnabledForActiveVehicle = true })
                        }
                    }

                    QGCTabBar {
                        id: tabBar
                        objectName: "joystickConfigurationTabs"
                        Layout.fillWidth: true
                        enabled: !joystickController.calibrating

                        QGCTabButton { objectName: "joystickGeneralTab"; text: qsTr("General") }
                        QGCTabButton { objectName: "joystickButtonsTab"; text: qsTr("Button Assignments") }
                        QGCTabButton {
                            objectName: "joystickCalibrationTab"
                            text: qsTr("Calibration")
                            enabled: configuration.joystickDevice.axisCount > 0
                        }
                        QGCTabButton { objectName: "joystickAdvancedTab"; text: qsTr("Advanced") }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        visible: tabBar.currentIndex === 0
                        text: qsTr("Move the sticks or press a button to check the response.")
                        wrapMode: Text.WordWrap
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        visible: tabBar.currentIndex === 2
                        text: joystickController.vehicle && joystickController.vehicle.armed
                              ? qsTr("Disarm the vehicle before calibrating.")
                              : qsTr("Click Calibrate, then follow the instructions and stick diagram.")
                        wrapMode: Text.WordWrap
                    }

                    RemoteControlCalibration {
                        id: remoteControlCalibration
                        objectName: "joystickAxisOverview"
                        Layout.fillWidth: true
                        Layout.maximumWidth: ScreenTools.defaultFontPixelWidth * 110
                        Layout.alignment: Qt.AlignLeft
                        visible: configuration.joystickDevice && (tabBar.currentIndex === 0 || tabBar.currentIndex === 2)
                                 && configuration.joystickDevice.axisCount > 0
                        controller: JoystickConfigController {
                            id: joystickController
                            objectName: "joystickPageController"
                            joystick: configuration.joystickDevice
                            statusText: remoteControlCalibration.statusText
                            cancelButton: remoteControlCalibration.cancelButton
                            nextButton: remoteControlCalibration.nextButton
                            joystickMode: true
                        }
                        useDeadband: configuration.joystickDevice ? configuration.joystickDevice.settings.useDeadband.rawValue : false
                        calibrationEnabled: !joystickController.vehicle || !joystickController.vehicle.armed
                        showCalibrationControls: tabBar.currentIndex === 2
                        showRawChannelMonitor: tabBar.currentIndex === 2
                        showExtensions: tabBar.currentIndex === 2
                        submersibleControls: joystickController.vehicle ? joystickController.vehicle.sub : false
                    }

                    JoystickComponentButtonMonitor {
                        Layout.fillWidth: true
                        visible: tabBar.currentIndex === 0
                        _joystick: configuration.joystickDevice
                    }

                    JoystickComponentButtons {
                        Layout.fillWidth: true
                        objectName: "joystickButtonAssignments"
                        joystick: configuration.joystickDevice
                        controller: joystickController
                        visible: tabBar.currentIndex === 1
                    }

                    JoystickComponentSettings {
                        Layout.fillWidth: true
                        objectName: "joystickAdvancedSettings"
                        joystick: configuration.joystickDevice
                        visible: tabBar.currentIndex === 3
                        enabled: !joystickController.calibrating
                    }
                }
            }
        }
    }
}
