/****************************************************************************
 *
 * DeepShark 3D attitude presentation panel.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick3D

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools

Rectangle {
    id: root

    readonly property var vehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property bool connected: vehicle !== null
    readonly property real rollAngle: connected && isFinite(vehicle.roll.rawValue) ? vehicle.roll.rawValue : 0
    readonly property real pitchAngle: connected && isFinite(vehicle.pitch.rawValue) ? vehicle.pitch.rawValue : 0
    readonly property real headingAngle: connected && isFinite(vehicle.heading.rawValue) ? vehicle.heading.rawValue : 0

    property bool compactMode: false
    property bool showCompactHeader: true
    property real cameraPitch: compactMode ? -12 : -18
    property real cameraYaw: -28
    property real cameraDistance: compactMode ? 650 : 570

    function resetView() {
        cameraPitch = compactMode ? -12 : -18
        cameraYaw = -28
        cameraDistance = compactMode ? 650 : 570
    }

    color: "#f7f9fb"
    border.color: "#aeb9c2"
    border.width: 1
    clip: true

    component FrameBeam: Model {
        source: "#Cube"
        castsShadows: true
        materials: PrincipledMaterial {
            baseColor: "#10171c"
            roughness: 0.42
            metalness: 0.72
        }
    }

    component DuctedThruster: Node {
        Model {
            source: "#Cylinder"
            scale: Qt.vector3d(0.36, 0.32, 0.36)
            castsShadows: true
            materials: PrincipledMaterial {
                baseColor: "#11191e"
                roughness: 0.36
                metalness: 0.75
            }
        }
        Model {
            source: "#Cylinder"
            y: -17
            scale: Qt.vector3d(0.27, 0.015, 0.27)
            materials: PrincipledMaterial {
                baseColor: "#263943"
                roughness: 0.32
                metalness: 0.5
            }
        }
        Model {
            source: "#Cylinder"
            y: -19
            scale: Qt.vector3d(0.07, 0.025, 0.07)
            materials: PrincipledMaterial {
                baseColor: "#4b6878"
                roughness: 0.3
                metalness: 0.65
            }
        }
    }

    View3D {
        anchors.fill: parent

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Color
            clearColor: "#f7f9fb"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            DirectionalLight {
                eulerRotation: Qt.vector3d(-45, -35, 0)
                brightness: 1.35
                color: "#e8f4ff"
                castsShadow: true
                shadowFactor: 55
            }

            DirectionalLight {
                eulerRotation: Qt.vector3d(28, 145, 0)
                brightness: 0.72
                color: "#54c8e8"
            }

            PointLight {
                position: Qt.vector3d(0, 180, -120)
                brightness: 14
                color: "#d7f3ff"
            }

            Node {
                eulerRotation: Qt.vector3d(root.cameraPitch, root.cameraYaw, 0)

                PerspectiveCamera {
                    id: sceneCamera
                    z: root.cameraDistance
                    clipNear: 10
                    clipFar: 3000
                    fieldOfView: 42
                }
            }

            Model {
                visible: !root.compactMode
                source: "#Rectangle"
                y: -112
                eulerRotation.x: -90
                scale: Qt.vector3d(7.5, 5.2, 1)
                receivesShadows: true
                materials: PrincipledMaterial {
                    baseColor: "#e7ecef"
                    roughness: 0.92
                    metalness: 0.05
                }
            }

            // Fixed world axes: X red, Y green, Z blue.
            Node {
                position: Qt.vector3d(-145, -82, 70)
                scale: root.compactMode ? Qt.vector3d(0.7, 0.7, 0.7) : Qt.vector3d(1, 1, 1)

                Model {
                    source: "#Cylinder"
                    x: 42
                    eulerRotation.z: -90
                    scale: Qt.vector3d(0.025, 0.42, 0.025)
                    materials: PrincipledMaterial { baseColor: "#dc2626"; roughness: 0.35 }
                }
                Model {
                    source: "#Cone"
                    x: 88
                    eulerRotation.z: -90
                    scale: Qt.vector3d(0.075, 0.14, 0.075)
                    materials: PrincipledMaterial { baseColor: "#dc2626"; roughness: 0.35 }
                }
                Model {
                    source: "#Cylinder"
                    y: 42
                    scale: Qt.vector3d(0.025, 0.42, 0.025)
                    materials: PrincipledMaterial { baseColor: "#16a34a"; roughness: 0.35 }
                }
                Model {
                    source: "#Cone"
                    y: 88
                    scale: Qt.vector3d(0.075, 0.14, 0.075)
                    materials: PrincipledMaterial { baseColor: "#16a34a"; roughness: 0.35 }
                }
                Model {
                    source: "#Cylinder"
                    z: -42
                    eulerRotation.x: -90
                    scale: Qt.vector3d(0.025, 0.42, 0.025)
                    materials: PrincipledMaterial { baseColor: "#2563eb"; roughness: 0.35 }
                }
                Model {
                    source: "#Cone"
                    z: -88
                    eulerRotation.x: -90
                    scale: Qt.vector3d(0.075, 0.14, 0.075)
                    materials: PrincipledMaterial { baseColor: "#2563eb"; roughness: 0.35 }
                }
            }

            Node {
                id: rov
                y: root.compactMode ? 2 : 0
                rotation: Quaternion.fromEulerAngles(Qt.vector3d(root.pitchAngle, -root.headingAngle, -root.rollAngle))

                Behavior on rotation {
                    QuaternionAnimation {
                        duration: 140
                        easing.type: Easing.OutCubic
                    }
                }

                // Main cylindrical pressure housing.
                Model {
                    source: "#Cylinder"
                    y: 26
                    z: 8
                    eulerRotation.x: 90
                    scale: Qt.vector3d(0.54, 1.08, 0.54)
                    castsShadows: true
                    materials: PrincipledMaterial {
                        baseColor: "#182126"
                        roughness: 0.38
                        metalness: 0.68
                    }
                }

                // Rear housing cap and front retaining ring.
                Model {
                    source: "#Cylinder"
                    y: 26
                    z: 69
                    eulerRotation.x: 90
                    scale: Qt.vector3d(0.59, 0.12, 0.59)
                    materials: PrincipledMaterial {
                        baseColor: "#0b1013"
                        roughness: 0.32
                        metalness: 0.8
                    }
                }
                Model {
                    source: "#Cylinder"
                    y: 26
                    z: -52
                    eulerRotation.x: 90
                    scale: Qt.vector3d(0.61, 0.13, 0.61)
                    materials: PrincipledMaterial {
                        baseColor: "#11181c"
                        roughness: 0.3
                        metalness: 0.82
                    }
                }

                // Transparent front camera dome and camera cluster.
                Model {
                    source: "#Sphere"
                    y: 26
                    z: -72
                    scale: Qt.vector3d(0.48, 0.48, 0.34)
                    materials: PrincipledMaterial {
                        baseColor: "#9dd7df"
                        roughness: 0.08
                        opacity: 0.48
                        metalness: 0.05
                    }
                }
                Model {
                    source: "#Sphere"
                    y: 26
                    z: -78
                    scale: Qt.vector3d(0.16, 0.16, 0.1)
                    materials: PrincipledMaterial {
                        baseColor: "#07131a"
                        roughness: 0.1
                        metalness: 0.25
                    }
                }

                // Top rectangular sonar/sensor assembly and support.
                FrameBeam {
                    y: 91
                    z: -17
                    scale: Qt.vector3d(0.74, 0.17, 0.25)
                }
                FrameBeam {
                    y: 72
                    z: -8
                    scale: Qt.vector3d(0.12, 0.22, 0.12)
                }
                Model {
                    source: "#Cube"
                    y: 91
                    z: -30
                    scale: Qt.vector3d(0.58, 0.11, 0.035)
                    materials: PrincipledMaterial {
                        baseColor: "#071015"
                        roughness: 0.12
                        metalness: 0.2
                    }
                }

                // Compact structural frame around the pressure housing.
                FrameBeam { x: -72; y: 0; z: 12; scale: Qt.vector3d(0.09, 0.09, 1.08) }
                FrameBeam { x: 72; y: 0; z: 12; scale: Qt.vector3d(0.09, 0.09, 1.08) }
                FrameBeam { x: -72; y: 58; z: 12; scale: Qt.vector3d(0.09, 0.09, 1.08) }
                FrameBeam { x: 72; y: 58; z: 12; scale: Qt.vector3d(0.09, 0.09, 1.08) }
                FrameBeam { y: -17; z: 12; scale: Qt.vector3d(1.55, 0.08, 0.1) }
                FrameBeam { y: 58; z: 12; scale: Qt.vector3d(1.55, 0.08, 0.1) }

                // Four vertical corner thrusters.
                DuctedThruster { position: Qt.vector3d(-91, 28, -31) }
                DuctedThruster { position: Qt.vector3d(91, 28, -31) }
                DuctedThruster { position: Qt.vector3d(-91, 28, 55) }
                DuctedThruster { position: Qt.vector3d(91, 28, 55) }

                // Two horizontal propulsion units.
                DuctedThruster {
                    position: Qt.vector3d(-84, -22, 25)
                    eulerRotation.z: 90
                }
                DuctedThruster {
                    position: Qt.vector3d(84, -22, 25)
                    eulerRotation.z: 90
                }

                // Forward inspection/tool rack, matching the long open frame silhouette.
                FrameBeam { x: -34; y: -42; z: -135; scale: Qt.vector3d(0.07, 0.07, 1.2) }
                FrameBeam { x: 34; y: -42; z: -135; scale: Qt.vector3d(0.07, 0.07, 1.2) }
                FrameBeam { y: -42; z: -91; scale: Qt.vector3d(0.72, 0.07, 0.07) }
                FrameBeam { y: -42; z: -132; scale: Qt.vector3d(0.72, 0.07, 0.07) }
                FrameBeam { y: -42; z: -173; scale: Qt.vector3d(0.72, 0.07, 0.07) }
                FrameBeam { y: -42; z: -214; scale: Qt.vector3d(0.72, 0.07, 0.07) }
                FrameBeam {
                    x: -43
                    y: -42
                    z: -226
                    eulerRotation.y: -18
                    scale: Qt.vector3d(0.07, 0.07, 0.42)
                }
                FrameBeam {
                    x: 43
                    y: -42
                    z: -226
                    eulerRotation.y: 18
                    scale: Qt.vector3d(0.07, 0.07, 0.42)
                }

                // Lower camera/payload platform.
                Model {
                    source: "#Cylinder"
                    y: -50
                    z: 3
                    scale: Qt.vector3d(0.62, 0.08, 0.62)
                    materials: PrincipledMaterial {
                        baseColor: "#0c1317"
                        roughness: 0.4
                        metalness: 0.72
                    }
                }
            }
        }

        camera: sceneCamera
    }

    MouseArea {
        anchors.fill: parent
        enabled: !root.compactMode
        acceptedButtons: Qt.LeftButton
        property real lastX
        property real lastY

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
                return
            }
            root.cameraYaw += (mouse.x - lastX) * 0.35
            root.cameraPitch = Math.max(-75, Math.min(55, root.cameraPitch + (mouse.y - lastY) * 0.3))
            lastX = mouse.x
            lastY = mouse.y
        }
        onWheel: function(wheel) {
            root.cameraDistance = Math.max(320, Math.min(950, root.cameraDistance - wheel.angleDelta.y * 0.45))
            wheel.accepted = true
        }
    }

    Rectangle {
        visible: !root.compactMode
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: ScreenTools.defaultFontPixelWidth * 1.5
        width: Math.min(parent.width * 0.42, ScreenTools.defaultFontPixelWidth * 42)
        height: telemetryColumn.implicitHeight + ScreenTools.defaultFontPixelWidth * 2
        radius: 4
        color: "#efffffff"
        border.color: "#aeb9c2"

        ColumnLayout {
            id: telemetryColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: ScreenTools.defaultFontPixelWidth
            spacing: ScreenTools.defaultFontPixelHeight * 0.35

            QGCLabel {
                Layout.fillWidth: true
                text: qsTr("ROV 3D 姿态")
                color: "#17212b"
                font.bold: true
                font.pointSize: ScreenTools.mediumFontPointSize
            }
            QGCLabel {
                text: root.connected ? qsTr("实时遥测") : qsTr("等待飞控连接")
                color: root.connected ? "#15803d" : "#b45309"
                font.bold: true
            }
            QGCLabel { text: qsTr("横滚  %1°").arg(root.rollAngle.toFixed(1)); color: "#334155" }
            QGCLabel { text: qsTr("俯仰  %1°").arg(root.pitchAngle.toFixed(1)); color: "#334155" }
            QGCLabel { text: qsTr("航向  %1°").arg(root.headingAngle.toFixed(1)); color: "#334155" }
        }
    }

    Rectangle {
        visible: root.compactMode && root.showCompactHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.max(ScreenTools.defaultFontPixelHeight * 2.1, 30)
        color: "#efffffff"
        border.color: "#aeb9c2"

        QGCLabel {
            anchors.left: parent.left
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("3D 姿态")
            color: "#17212b"
            font.bold: true
        }

        QGCLabel {
            anchors.right: parent.right
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("横滚 %1°  俯仰 %2°").arg(root.rollAngle.toFixed(1)).arg(root.pitchAngle.toFixed(1))
            color: root.connected ? "#0369a1" : "#64748b"
            font.pointSize: ScreenTools.defaultFontPointSize * 0.78
        }
    }

    Row {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: ScreenTools.defaultFontPixelWidth
        anchors.bottomMargin: root.compactMode
                              ? ScreenTools.defaultFontPixelHeight * 0.45
                              : ScreenTools.defaultFontPixelHeight
        spacing: ScreenTools.defaultFontPixelWidth * 0.8

        QGCLabel { text: "X"; color: "#dc2626"; font.bold: true }
        QGCLabel { text: "Y"; color: "#16a34a"; font.bold: true }
        QGCLabel { text: "Z"; color: "#2563eb"; font.bold: true }
    }

    QGCButton {
        visible: !root.compactMode
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: ScreenTools.defaultFontPixelWidth * 1.5
        text: qsTr("复位视角")
        backgroundColor: "#e2e8f0"
        textColor: "#17212b"
        showBorder: true
        onClicked: root.resetView()
    }

    QGCLabel {
        visible: !root.compactMode
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: ScreenTools.defaultFontPixelHeight
        text: qsTr("拖动旋转视角  |  滚轮缩放")
        color: "#64748b"
        font.pointSize: ScreenTools.defaultFontPointSize * 0.82
    }
}
