/****************************************************************************
 *
 * DeepShark 3D attitude presentation panel.
 *
 ****************************************************************************/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick3D

import QGroundControl
import QGroundControl.Controls

Rectangle {
    id: root
    objectName: "attitude3DPanel"

    property var vehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property bool connected: vehicle !== null
    readonly property bool communicationLost: connected && vehicle.vehicleLinkManager.communicationLost
    readonly property bool attitudeValid: connected && isFinite(vehicle.roll.rawValue)
                                         && isFinite(vehicle.pitch.rawValue) && isFinite(vehicle.heading.rawValue)
    readonly property real rollAngle: connected && isFinite(vehicle.roll.rawValue) ? vehicle.roll.rawValue : 0
    readonly property real pitchAngle: connected && isFinite(vehicle.pitch.rawValue) ? vehicle.pitch.rawValue : 0
    readonly property real headingAngle: connected && isFinite(vehicle.heading.rawValue) ? vehicle.heading.rawValue : 0
    readonly property bool depthValid: connected && !communicationLost && !!vehicle.altitudeRelative
                                      && isFinite(vehicle.altitudeRelative.rawValue)
    readonly property real depthMeters: depthValid ? -vehicle.altitudeRelative.rawValue : NaN
    // The simplified model is about 330 units long; use an approximate 1.1 m visual scale.
    readonly property real sceneUnitsPerMeter: 300
    readonly property real depthScaleMaximum: depthValid ? Math.max(2, Math.ceil(depthMeters * 2) / 2) : 2
    property bool componentReady: false
    property real waterPhase: 0

    property bool compactMode: false
    property bool showCompactHeader: true
    property QtObject viewState: localViewState
    readonly property var viewModeLabels: [qsTr("惯性跟随"), qsTr("固定视角"), qsTr("俯视"),
                                          qsTr("左侧"), qsTr("右侧"), qsTr("前视"), qsTr("自由观察"), qsTr("硬跟随")]
    readonly property real renderedHeading: 2 * Math.atan2(headingNode.rotation.y, headingNode.rotation.scalar) * 180 / Math.PI
    readonly property real cameraWorldYaw: viewState.cameraYaw + (viewState.followsHeading
        ? (viewState.inertialFollow && viewState.followInitialized ? viewState.followYaw : renderedHeading) : 0)
    readonly property string depthText: depthValid ? qsTr("深度 %1 m").arg(depthMeters.toFixed(2)) : qsTr("深度 —")
    readonly property string attitudeStatus: !connected ? qsTr("等待飞控连接")
                                            : communicationLost ? qsTr("通信中断")
                                            : !attitudeValid ? qsTr("等待姿态数据") : qsTr("姿态遥测")
    signal expandRequested()

    Attitude3DViewState { id: localViewState }

    function synchronizeVehicle() {
        if (!componentReady) {
            return
        }
        if (viewState.trackedVehicle !== vehicle) {
            viewState.trackedVehicle = vehicle
            viewState.vehicleY = 0
            viewState.hasDepth = false
            viewState.followInitialized = false
        }
        if (depthValid) {
            viewState.vehicleY = -depthMeters * sceneUnitsPerMeter
            viewState.hasDepth = true
        }
        if (attitudeValid && !communicationLost) {
            viewState.initializeFollow(renderedHeading, headingNode.y, vehicle)
        }
    }

    onVehicleChanged: synchronizeVehicle()
    onViewStateChanged: synchronizeVehicle()
    onDepthMetersChanged: synchronizeVehicle()
    onAttitudeValidChanged: synchronizeVehicle()
    Component.onCompleted: {
        componentReady = true
        synchronizeVehicle()
    }

    FrameAnimation {
        running: root.visible && root.Window.window !== null
                 && (root.depthValid || (root.viewState.inertialFollow && root.attitudeValid && !root.communicationLost))
        onTriggered: {
            root.waterPhase = (root.waterPhase + frameTime * 0.65) % (Math.PI * 2)
            if (root.viewState.inertialFollow && root.attitudeValid && !root.communicationLost) {
                root.viewState.advanceFollow(frameTime, root.renderedHeading, headingNode.y,
                                             root.compactMode ? 20 : 30, root.vehicle)
            }
        }
    }

    function resetView() {
        viewState.resetView()
    }

    function selectViewMode(mode) {
        if (mode === Attitude3DViewState.Free) {
            viewState.enterFreeView(cameraWorldYaw)
        } else {
            viewState.selectView(mode)
        }
    }

    function angleText(angle) {
        return attitudeValid ? angle.toFixed(1) + "°" : "—"
    }

    color: "#f7f9fb"
    border.color: "#aeb9c2"
    border.width: 1
    clip: true

    component FrameBeam: Model {
        source: "#Cube"
        castsShadows: true
        materials: PrincipledMaterial {
            baseColor: "#405663"
            roughness: 0.52
            metalness: 0.28
        }
    }

    component DuctedThruster: Node {
        Model {
            source: "#Cylinder"
            scale: Qt.vector3d(0.36, 0.32, 0.36)
            castsShadows: true
            materials: PrincipledMaterial {
                baseColor: "#304754"
                roughness: 0.36
                metalness: 0.3
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
            clearColor: "#174858"
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            fog: Fog {
                enabled: true
                color: "#174858"
                density: 0.45
                depthEnabled: true
                depthNear: 900
                depthFar: 3500
            }
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
                position: Qt.vector3d(0, root.viewState.vehicleY + 180, -120)
                brightness: 14
                color: "#d7f3ff"
            }

            Node {
                id: cameraRig
                y: root.viewState.inertialFollow && root.viewState.followInitialized
                   ? root.viewState.followHeight : headingNode.y
                rotation: root.viewState.followsHeading
                          ? (root.viewState.inertialFollow && root.viewState.followInitialized
                             ? Quaternion.fromEulerAngles(Qt.vector3d(0, root.viewState.followYaw, 0))
                             : headingNode.rotation) : Qt.quaternion(1, 0, 0, 0)

                DirectionalLight {
                    eulerRotation: Qt.vector3d(-20, -15, 0)
                    brightness: 0.8
                    color: "#ddf5ff"
                }

                Node {
                    eulerRotation: Qt.vector3d(root.viewState.cameraPitch, root.viewState.cameraYaw, 0)

                    PerspectiveCamera {
                        id: sceneCamera
                        objectName: "attitudeCamera"
                        z: root.viewState.cameraDistance
                        clipNear: 10
                        clipFar: 3000
                        fieldOfView: 42
                    }
                }
            }

            Model {
                objectName: "attitudeWaterSurface"
                visible: root.viewState.hasDepth
                castsShadows: false
                receivesShadows: false
                source: "#Rectangle"
                y: 0
                eulerRotation.x: -90
                scale: Qt.vector3d(18, 18, 1)
                materials: CustomMaterial {
                    property real wavePhase: root.waterPhase
                    property color waterColor: "#237ad3d5"
                    shadingMode: CustomMaterial.Shaded
                    cullMode: Material.NoCulling
                    sourceBlend: CustomMaterial.SrcAlpha
                    destinationBlend: CustomMaterial.OneMinusSrcAlpha
                    fragmentShader: "qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/WaterSurface.frag"
                }
            }

            Repeater3D {
                model: 13
                delegate: Node {
                    id: gridLine
                    required property int index
                    visible: root.viewState.hasDepth
                    Model {
                        source: "#Cube"
                        position: Qt.vector3d((gridLine.index - 6) * 50, 1, 0)
                        scale: Qt.vector3d(0.012, 0.012, 6)
                        castsShadows: false
                        materials: PrincipledMaterial { baseColor: "#8fe3e7"; opacity: 0.12; lighting: PrincipledMaterial.NoLighting }
                    }
                    Model {
                        source: "#Cube"
                        position: Qt.vector3d(0, 1, (gridLine.index - 6) * 50)
                        scale: Qt.vector3d(6, 0.012, 0.012)
                        castsShadows: false
                        materials: PrincipledMaterial { baseColor: "#8fe3e7"; opacity: 0.12; lighting: PrincipledMaterial.NoLighting }
                    }
                }
            }

            // Fixed world axes: X red, Y green, Z blue.
            Node {
                position: Qt.vector3d(-145, headingNode.y - 82, 70)
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
                id: headingNode
                objectName: "attitudeHeading"
                y: root.viewState.vehicleY
                rotation: Quaternion.fromEulerAngles(Qt.vector3d(0, -root.headingAngle, 0))

                Behavior on y {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }

                Behavior on rotation {
                    QuaternionAnimation { duration: 140; easing.type: Easing.OutCubic }
                }

                Node {
                    id: rov
                    objectName: "attitudeBody"
                    y: root.compactMode ? 2 : 0
                    rotation: Quaternion.fromEulerAngles(Qt.vector3d(root.pitchAngle, 0, -root.rollAngle))

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
                            baseColor: "#465f6c"
                            roughness: 0.38
                            metalness: 0.3
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
        }

        camera: sceneCamera
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        property real lastX
        property real lastY

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: function(mouse) {
            if (!pressed || root.compactMode) {
                return
            }
            if (root.viewState.viewMode !== Attitude3DViewState.Free) {
                root.viewState.enterFreeView(root.cameraWorldYaw)
            }
            root.viewState.cameraYaw += (mouse.x - lastX) * 0.35
            root.viewState.cameraPitch = Math.max(-85, Math.min(55,
                root.viewState.cameraPitch + (mouse.y - lastY) * 0.3))
            lastX = mouse.x
            lastY = mouse.y
        }
        onDoubleClicked: {
            if (root.compactMode) {
                root.expandRequested()
            } else {
                root.resetView()
            }
        }
        onWheel: function(wheel) {
            root.viewState.cameraDistance = Math.max(320,
                Math.min(950, root.viewState.cameraDistance - wheel.angleDelta.y * 0.45))
            wheel.accepted = true
        }
    }

    Rectangle {
        id: topHud
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.compactMode ? 4 : 12
        height: root.compactMode ? 28 : 42
        radius: 4
        color: "#edffffff"
        border.color: "#bdcbd4"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 3
            spacing: root.compactMode ? 4 : 12

            QGCLabel {
                visible: !root.compactMode || root.showCompactHeader
                text: qsTr("ROV 姿态")
                color: "#17212b"
                font.bold: true
            }
            ComboBox {
                objectName: "attitudeViewSelector"
                Layout.preferredWidth: root.compactMode ? 102 : 148
                Layout.fillHeight: true
                model: root.viewModeLabels
                currentIndex: root.viewState.viewMode
                font.pointSize: ScreenTools.defaultFontPointSize * (root.compactMode ? 0.75 : 0.9)
                onActivated: function(index) { root.selectViewMode(index) }
            }
            Item { Layout.fillWidth: true }
            QGCLabel {
                text: qsTr("航向 %1").arg(root.angleText(root.headingAngle))
                color: "#0369a1"
                font.bold: true
                font.pointSize: ScreenTools.defaultFontPointSize * (root.compactMode ? 0.75 : 1)
            }
            QGCButton {
                visible: !root.compactMode
                text: qsTr("回到跟随")
                backgroundColor: "#dceaf2"
                textColor: "#17212b"
                onClicked: root.resetView()
            }
        }
    }

    Rectangle {
        id: bottomHud
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: root.compactMode ? 4 : 12
        height: root.compactMode ? 35 : 52
        radius: 4
        color: "#edffffff"
        border.color: "#bdcbd4"

        Column {
            anchors.fill: parent
            anchors.margins: root.compactMode ? 3 : 6
            spacing: 2
            QGCLabel {
                width: parent.width
                text: qsTr("%1   横滚 %2   俯仰 %3").arg(root.depthText)
                      .arg(root.angleText(root.rollAngle)).arg(root.angleText(root.pitchAngle))
                color: "#334155"
                font.bold: true
                font.pointSize: ScreenTools.defaultFontPointSize * (root.compactMode ? 0.72 : 1)
            }
            QGCLabel {
                width: parent.width
                text: root.compactMode ? root.attitudeStatus
                      : qsTr("%1 · 拖动自由观察，滚轮缩放，双击回到跟随").arg(root.attitudeStatus)
                color: root.connected && !root.communicationLost && root.attitudeValid ? "#15803d" : "#b45309"
                font.pointSize: ScreenTools.defaultFontPointSize * (root.compactMode ? 0.65 : 0.8)
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        id: depthRuler
        objectName: "attitudeDepthRuler"
        visible: root.viewState.hasDepth
        anchors.right: parent.right
        anchors.rightMargin: root.compactMode ? 7 : 18
        anchors.top: topHud.bottom
        anchors.bottom: bottomHud.top
        anchors.topMargin: 7
        anchors.bottomMargin: 7
        width: root.compactMode ? 36 : 64
        radius: 4
        color: "#70102734"
        border.color: "#6094d1dc"

        QGCLabel {
            id: rulerTitle
            anchors.top: parent.top
            anchors.topMargin: 3
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.compactMode ? qsTr("水面") : qsTr("水体示意")
            color: "#dbf8ff"
            font.pointSize: ScreenTools.defaultFontPointSize * (root.compactMode ? 0.6 : 0.75)
        }

        Item {
            id: rulerTrack
            anchors.top: rulerTitle.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 5
            anchors.bottomMargin: root.compactMode ? 10 : 16

            Repeater {
                model: 5
                delegate: Item {
                    id: depthTick
                    required property int index
                    width: rulerTrack.width
                    y: depthTick.index * rulerTrack.height / 4
                    Rectangle {
                        width: root.compactMode ? 6 : 10
                        height: 1
                        color: "#b8e9f2"
                    }
                    QGCLabel {
                        anchors.right: parent.right
                        anchors.rightMargin: 3
                        y: -height / 2
                        text: (root.depthScaleMaximum * depthTick.index / 4).toFixed(1)
                        color: "#dbf8ff"
                        font.pointSize: ScreenTools.defaultFontPointSize * (root.compactMode ? 0.62 : 0.8)
                    }
                }
            }

            Rectangle {
                objectName: "attitudeDepthMarker"
                visible: root.depthValid
                y: Math.max(0, Math.min(1, root.depthMeters / root.depthScaleMaximum)) * rulerTrack.height - 1
                width: rulerTrack.width
                height: 2
                color: "#37e8ed"
            }
        }
    }
}
