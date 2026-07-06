/****************************************************************************
 *
 * DeepShark AUV mission workspace.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtLocation
import QtPositioning

import DeepShark 1.0
import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightMap
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle
    property bool debugRunning: false
    property bool debugPaused: false
    property real debugProgress: 0
    property string debugMode: "mission"
    property var debugPosition: QtPositioning.coordinate()
    property var returnStartPosition: QtPositioning.coordinate()
    property real returnProgress: 0
    property real returnDistance: 0
    property real debugDepth: 0
    property real debugSpeed: 0
    property real debugHeading: 75
    property real debugBattery: 93
    property real elapsedSeconds: 0
    property real debugRate: 1
    property bool emergencyLatched: false
    property var eventRows: []
    property bool compact: width < ScreenTools.defaultFontPixelWidth * 105

    readonly property var stages: [
        { name: qsTr("岸端布放"), detail: qsTr("检查链路并从近岸下水") },
        { name: qsTr("建立定深"), detail: qsTr("下潜至 2-4 米巡检深度") },
        { name: qsTr("坝体巡检"), detail: qsTr("沿主坝近水侧低速航行") },
        { name: qsTr("检查点"), detail: qsTr("记录坝面与附属结构状态") },
        { name: qsTr("平行返航"), detail: qsTr("沿安全间距返回布放区") },
        { name: qsTr("岸端回收"), detail: qsTr("上浮并等待人工回收") }
    ]
    readonly property int debugStage: debugMode === "return" ? 4
                                      : debugMode === "surface" ? 5
                                      : Math.min(stages.length - 1, Math.floor(debugProgress * stages.length))
    readonly property var vehicleCoordinate: activeVehicle && activeVehicle.coordinate && activeVehicle.coordinate.isValid
                                                  ? activeVehicle.coordinate : QtPositioning.coordinate()
    readonly property var displayCoordinate: DeepSharkAuvController.debugMode
                                                 ? (debugPosition.isValid ? debugPosition : debugCoordinate())
                                                 : vehicleCoordinate
    readonly property real displayDepth: DeepSharkAuvController.debugMode
                                             ? debugDepth
                                             : activeVehicle && activeVehicle.altitudeRelative
                                               ? Math.abs(activeVehicle.altitudeRelative.rawValue) : 0
    readonly property real displaySpeed: DeepSharkAuvController.debugMode
                                             ? debugSpeed
                                             : activeVehicle && activeVehicle.groundSpeed
                                               ? activeVehicle.groundSpeed.rawValue : 0
    readonly property real displayHeading: DeepSharkAuvController.debugMode
                                               ? debugHeading
                                               : activeVehicle && activeVehicle.heading
                                                 ? activeVehicle.heading.rawValue : 0

    color: "#081018"
    border.color: "#263746"
    border.width: 1
    clip: true

    function addEvent(message) {
        var rows = eventRows.slice(0)
        rows.unshift(Qt.formatTime(new Date(), "hh:mm:ss") + "  " + message)
        if (rows.length > 30) {
            rows.length = 30
        }
        eventRows = rows
    }

    function debugCoordinate() {
        var points = DeepSharkAuvController.waypoints
        if (points.length === 0) {
            var angle = debugProgress * Math.PI * 2
            return QtPositioning.coordinate(
                        DeepSharkAuvController.mapLatitude + Math.sin(angle) * 0.012,
                        DeepSharkAuvController.mapLongitude + Math.cos(angle) * 0.018)
        }
        var scaled = Math.min(0.999, debugProgress) * Math.max(1, points.length - 1)
        var index = Math.floor(scaled)
        var nextIndex = Math.min(points.length - 1, index + 1)
        var fraction = scaled - index
        return QtPositioning.coordinate(
                    points[index].latitude + (points[nextIndex].latitude - points[index].latitude) * fraction,
                    points[index].longitude + (points[nextIndex].longitude - points[index].longitude) * fraction)
    }

    function debugRouteHeading() {
        var points = DeepSharkAuvController.waypoints
        if (points.length < 2) {
            return 0
        }
        var scaled = Math.min(0.999, debugProgress) * (points.length - 1)
        var index = Math.floor(scaled)
        var nextIndex = Math.min(points.length - 1, index + 1)
        var latitudeScale = Math.cos(points[index].latitude * Math.PI / 180)
        var east = (points[nextIndex].longitude - points[index].longitude) * latitudeScale
        var north = points[nextIndex].latitude - points[index].latitude
        return (Math.atan2(east, north) * 180 / Math.PI + 360) % 360
    }

    function coordinateDistance(a, b) {
        if (!a.isValid || !b.isValid) {
            return 0
        }
        return a.distanceTo(b)
    }

    function routeStartCoordinate() {
        var points = DeepSharkAuvController.waypoints
        return points.length > 0
               ? QtPositioning.coordinate(points[0].latitude, points[0].longitude)
               : QtPositioning.coordinate()
    }

    function interpolateCoordinate(fromCoordinate, toCoordinate, fraction) {
        return QtPositioning.coordinate(
                    fromCoordinate.latitude + (toCoordinate.latitude - fromCoordinate.latitude) * fraction,
                    fromCoordinate.longitude + (toCoordinate.longitude - fromCoordinate.longitude) * fraction)
    }

    function missionPath() {
        var path = []
        var points = DeepSharkAuvController.waypoints
        for (var i = 0; i < points.length; i++) {
            path.push(QtPositioning.coordinate(points[i].latitude, points[i].longitude))
        }
        return path
    }

    function resetDebug() {
        debugRunning = false
        debugPaused = false
        debugMode = "mission"
        debugProgress = 0
        debugPosition = routeStartCoordinate()
        returnProgress = 0
        returnDistance = 0
        debugDepth = 0
        debugSpeed = 0
        debugHeading = 75
        debugBattery = 93
        elapsedSeconds = 0
        emergencyLatched = false
        addEvent(qsTr("调试状态已复位"))
    }

    function startDebug() {
        if (!DeepSharkAuvController.debugMode) {
            addEvent(qsTr("请先开启调试模式"))
            return
        }
        if (debugProgress >= 1) {
            resetDebug()
        }
        debugRunning = true
        debugPaused = false
        debugMode = "mission"
        debugPosition = debugCoordinate()
        addEvent(qsTr("调试任务开始运行"))
    }

    function toggleDebugPause() {
        if (!debugRunning) {
            return
        }
        debugPaused = !debugPaused
        addEvent(debugPaused ? qsTr("调试任务已暂停") : qsTr("调试任务继续"))
    }

    function updateDebug() {
        if (!DeepSharkAuvController.debugMode || !debugRunning || debugPaused) {
            return
        }
        var simulatedStep = 0.25 * debugRate
        elapsedSeconds += simulatedStep

        if (debugMode === "return") {
            var home = routeStartCoordinate()
            returnProgress = Math.min(1, returnProgress + DeepSharkAuvController.defaultSpeed * simulatedStep / Math.max(1, returnDistance))
            debugPosition = interpolateCoordinate(returnStartPosition, home, returnProgress)
            debugHeading = returnStartPosition.azimuthTo(home)
            debugSpeed = DeepSharkAuvController.defaultSpeed
            if (returnProgress >= 1) {
                debugRunning = false
                debugSpeed = 0
                addEvent(qsTr("已返回 1 号岸端布放点"))
            }
        } else if (debugMode === "surface") {
            debugDepth = Math.max(0, debugDepth - 0.48 * simulatedStep)
            debugSpeed = 0
            if (debugDepth <= 0) {
                debugRunning = false
                addEvent(qsTr("已在当前位置完成上浮"))
            }
        } else {
            var routeMeters = Math.max(1, DeepSharkAuvController.totalDistance)
            debugProgress = Math.min(1, debugProgress + DeepSharkAuvController.defaultSpeed * simulatedStep / routeMeters)
            debugPosition = debugCoordinate()
            debugDepth = debugProgress < 0.16
                         ? DeepSharkAuvController.defaultDepth * debugProgress / 0.16
                         : debugProgress > 0.84
                           ? DeepSharkAuvController.defaultDepth * (1 - debugProgress) / 0.16
                           : DeepSharkAuvController.defaultDepth + Math.sin(elapsedSeconds / 5) * 0.15
            debugSpeed = debugProgress > 0.16 && debugProgress < 0.84 ? DeepSharkAuvController.defaultSpeed : 0.35
            debugHeading = debugRouteHeading()
            if (debugProgress >= 1) {
                debugRunning = false
                debugSpeed = 0
                addEvent(qsTr("调试任务已完成"))
            }
        }
        debugBattery = Math.max(25, debugBattery - 0.08 * simulatedStep)
        if (DeepSharkAuvController.followVehicle && displayCoordinate.isValid) {
            missionMap.center = displayCoordinate
        }
    }

    function requestReturn() {
        var home = routeStartCoordinate()
        if (!home.isValid || !displayCoordinate.isValid) {
            addEvent(qsTr("无法确定返航位置"))
            return
        }
        debugMode = "return"
        returnStartPosition = displayCoordinate
        returnDistance = coordinateDistance(returnStartPosition, home)
        returnProgress = 0
        debugRunning = true
        debugPaused = false
        addEvent(qsTr("开始直接返航至 1 号岸端布放点"))
    }

    function requestSurface() {
        debugMode = "surface"
        debugPosition = displayCoordinate
        debugRunning = debugDepth > 0
        debugPaused = false
        debugSpeed = 0
        addEvent(qsTr("开始在当前位置上浮"))
    }

    function emergencyStop() {
        emergencyLatched = true
        debugRunning = false
        debugPaused = false
        debugSpeed = 0
        debugMode = "stopped"
        addEvent(qsTr("急停已确认：推进停止，位置保持"))
    }

    function formatElapsed(totalSeconds) {
        var minutes = Math.floor(totalSeconds / 60)
        var seconds = totalSeconds % 60
        return (minutes < 10 ? "0" : "") + minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function fitMission() {
        var points = DeepSharkAuvController.waypoints
        if (points.length === 0) {
            missionMap.center = QtPositioning.coordinate(DeepSharkAuvController.mapLatitude,
                                                         DeepSharkAuvController.mapLongitude)
            missionMap.zoomLevel = DeepSharkAuvController.mapZoom
            return
        }
        var minLat = points[0].latitude
        var maxLat = points[0].latitude
        var minLon = points[0].longitude
        var maxLon = points[0].longitude
        for (var i = 1; i < points.length; i++) {
            minLat = Math.min(minLat, points[i].latitude)
            maxLat = Math.max(maxLat, points[i].latitude)
            minLon = Math.min(minLon, points[i].longitude)
            maxLon = Math.max(maxLon, points[i].longitude)
        }
        missionMap.setVisibleRegion(QtPositioning.rectangle(
                                        QtPositioning.coordinate(maxLat, minLon),
                                        QtPositioning.coordinate(minLat, maxLon)))
    }

    Timer {
        interval: 250
        repeat: true
        running: root.visible
        onTriggered: root.updateDebug()
    }

    QGCFileDialog {
        id: missionFileDialog
        title: qsTr("导入 QGroundControl 任务")
        nameFilters: [qsTr("QGroundControl 任务 (*.plan)")]
        onAcceptedForLoad: (file) => {
            if (DeepSharkAuvController.importPlan(file)) {
                root.addEvent(qsTr("已导入任务：%1").arg(DeepSharkAuvController.missionName))
                Qt.callLater(root.fitMission)
            } else {
                root.addEvent(qsTr("任务导入失败"))
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Math.max(5, ScreenTools.defaultFontPixelWidth * 0.7)
        spacing: Math.max(5, ScreenTools.defaultFontPixelWidth * 0.65)

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(44, ScreenTools.defaultFontPixelHeight * 2.9)
            color: "#0d1821"
            border.color: "#2b4252"
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelWidth * 0.8
                spacing: ScreenTools.defaultFontPixelWidth * 0.7

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QGCLabel {
                        Layout.fillWidth: true
                        text: DeepSharkAuvController.missionName.length > 0
                              ? DeepSharkAuvController.missionName : qsTr("AUV 任务工作区")
                        color: "#f8fafc"
                        font.bold: true
                        font.pointSize: ScreenTools.mediumFontPointSize
                        elide: Text.ElideRight
                    }
                    QGCLabel {
                        Layout.fillWidth: true
                        text: DeepSharkAuvController.missionSummary.length > 0
                              ? DeepSharkAuvController.missionSummary
                              : qsTr("怀柔水库主坝 · 岸基短程巡检")
                        color: "#8fb3c8"
                        elide: Text.ElideRight
                    }
                }

                QGCLabel {
                    visible: !root.compact
                    text: DeepSharkAuvController.debugMode ? qsTr("调试模式") : qsTr("实机数据")
                    color: DeepSharkAuvController.debugMode ? "#fbbf24" : "#4ade80"
                    font.bold: true
                }
                Switch {
                    checked: DeepSharkAuvController.debugMode
                    onToggled: {
                        DeepSharkAuvController.debugMode = checked
                        root.addEvent(checked ? qsTr("调试模式已开启") : qsTr("调试模式已关闭"))
                        if (!checked) root.resetDebug()
                    }
                }
                QGCLabel {
                    visible: DeepSharkAuvController.debugMode
                    text: qsTr("倍速")
                    color: "#cbd5e1"
                }
                QGCTextField {
                    id: debugRateField
                    visible: DeepSharkAuvController.debugMode
                    Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 7
                    text: root.debugRate.toString()
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator {
                        bottom: 0.1
                        top: 100
                        decimals: 1
                        notation: DoubleValidator.StandardNotation
                    }
                    onEditingFinished: {
                        var value = Number(text)
                        root.debugRate = isFinite(value) ? Math.max(0.1, Math.min(100, value)) : 1
                        text = root.debugRate.toString()
                    }
                }
                QGCButton { text: qsTr("导入任务"); onClicked: missionFileDialog.openForLoad() }
                QGCButton { text: qsTr("设置"); onClicked: settingsOverlay.visible = true }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Math.max(5, ScreenTools.defaultFontPixelWidth * 0.65)

            Rectangle {
                Layout.preferredWidth: root.compact ? Math.max(155, root.width * 0.22) : Math.max(190, root.width * 0.18)
                Layout.fillHeight: true
                color: "#0b151d"
                border.color: "#263b49"
                radius: 4
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: ScreenTools.defaultFontPixelWidth * 0.75
                    spacing: ScreenTools.defaultFontPixelHeight * 0.35

                    QGCLabel { text: qsTr("任务流程"); color: "#f8fafc"; font.bold: true }

                    Repeater {
                        model: root.stages
                        Rectangle {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 34
                            color: DeepSharkAuvController.debugMode && index === root.debugStage ? "#123b4d" : "#0d202a"
                            border.color: DeepSharkAuvController.debugMode && index === root.debugStage ? "#38bdf8" : "#263b49"
                            radius: 3

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 7
                                Rectangle {
                                    width: 20; height: 20; radius: 10
                                    color: DeepSharkAuvController.debugMode && index < root.debugStage ? "#16a34a"
                                           : DeepSharkAuvController.debugMode && index === root.debugStage ? "#0284c7"
                                           : "#334155"
                                    QGCLabel { anchors.centerIn: parent; text: String(index + 1); color: "white"; font.bold: true }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    QGCLabel { Layout.fillWidth: true; text: modelData.name; color: "#e5edf3"; font.bold: true }
                                    QGCLabel {
                                        Layout.fillWidth: true
                                        text: modelData.detail
                                        color: "#8299a8"
                                        font.pointSize: ScreenTools.smallFontPointSize
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#263b49" }
                    QGCLabel { text: qsTr("任务控制"); color: "#f8fafc"; font.bold: true }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        QGCButton {
                            Layout.fillWidth: true
                            text: root.debugRunning ? qsTr("运行中") : qsTr("启动")
                            enabled: DeepSharkAuvController.debugMode && !root.debugRunning && !root.emergencyLatched
                            onClicked: root.startDebug()
                        }
                        QGCButton {
                            Layout.fillWidth: true
                            text: root.debugPaused ? qsTr("继续") : qsTr("暂停")
                            enabled: DeepSharkAuvController.debugMode && root.debugRunning && !root.emergencyLatched
                            onClicked: root.toggleDebugPause()
                        }
                        QGCButton {
                            Layout.fillWidth: true
                            text: qsTr("返航")
                            enabled: DeepSharkAuvController.debugMode && !root.emergencyLatched
                            onClicked: root.requestReturn()
                        }
                        QGCButton {
                            Layout.fillWidth: true
                            text: qsTr("上浮")
                            enabled: DeepSharkAuvController.debugMode && !root.emergencyLatched
                            onClicked: root.requestSurface()
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(48, ScreenTools.defaultFontPixelHeight * 3)
                        radius: 4
                        color: root.emergencyLatched ? "#7f1d1d" : "#991b1b"
                        border.color: "#f87171"
                        border.width: 2

                        QGCLabel {
                            anchors.centerIn: parent
                            text: root.emergencyLatched ? qsTr("急停已触发") : qsTr("向右滑动确认急停")
                            color: "#fff7ed"
                            font.bold: true
                        }

                        Slider {
                            id: emergencySlider
                            anchors.fill: parent
                            anchors.margins: 4
                            from: 0
                            to: 100
                            value: root.emergencyLatched ? 100 : 0
                            enabled: DeepSharkAuvController.debugMode && !root.emergencyLatched
                            opacity: 0.88

                            background: Rectangle {
                                x: emergencySlider.leftPadding
                                y: emergencySlider.topPadding + emergencySlider.availableHeight / 2 - height / 2
                                width: emergencySlider.availableWidth
                                height: 8
                                radius: 4
                                color: "#3f0d0d"
                                Rectangle {
                                    width: emergencySlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: 4
                                    color: "#ef4444"
                                }
                            }
                            handle: Rectangle {
                                x: emergencySlider.leftPadding + emergencySlider.visualPosition * (emergencySlider.availableWidth - width)
                                y: emergencySlider.topPadding + emergencySlider.availableHeight / 2 - height / 2
                                width: 34
                                height: 34
                                radius: 17
                                color: "#fff7ed"
                                border.color: "#ef4444"
                                border.width: 2
                                QGCLabel { anchors.centerIn: parent; text: "▶"; color: "#b91c1c"; font.bold: true }
                            }

                            onMoved: {
                                if (value >= 96) {
                                    root.emergencyStop()
                                }
                            }
                            onPressedChanged: {
                                if (!pressed && !root.emergencyLatched && value < 96) {
                                    value = 0
                                }
                            }
                        }
                    }
                    QGCButton {
                        Layout.fillWidth: true
                        visible: root.emergencyLatched
                        text: qsTr("复位急停并返回待命")
                        onClicked: root.resetDebug()
                    }
                    QGCLabel {
                        Layout.fillWidth: true
                        text: DeepSharkAuvController.debugMode
                              ? qsTr("调试控制仅改变本地界面，不向载具发送指令。")
                              : qsTr("实机模式下控制按钮锁定，避免误操作。")
                        color: "#fbbf24"
                        wrapMode: Text.WordWrap
                        font.pointSize: ScreenTools.smallFontPointSize
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#071018"
                border.color: "#263b49"
                radius: 4
                clip: true

                FlightMap {
                    id: missionMap
                    anchors.fill: parent
                    mapName: "DeepSharkAuvMission"
                    center: QtPositioning.coordinate(DeepSharkAuvController.mapLatitude,
                                                     DeepSharkAuvController.mapLongitude)
                    zoomLevel: DeepSharkAuvController.mapZoom
                    allowGCSLocationCenter: false
                    allowVehicleLocationCenter: false

                    function selectPreferredMapType() {
                        if (!mapReady || !DeepSharkAuvController.preferSatellite) {
                            updateActiveMapType()
                            return
                        }
                        for (var i = 0; i < supportedMapTypes.length; i++) {
                            var name = supportedMapTypes[i].name
                            if (name.indexOf("Satellite") >= 0 || name.indexOf("Hybrid") >= 0) {
                                activeMapType = supportedMapTypes[i]
                                return
                            }
                        }
                        updateActiveMapType()
                    }

                    onMapReadyChanged: if (mapReady) selectPreferredMapType()
                    onCenterChanged: {
                        if (!DeepSharkAuvController.followVehicle) {
                            DeepSharkAuvController.mapLatitude = center.latitude
                            DeepSharkAuvController.mapLongitude = center.longitude
                        }
                    }
                    onZoomLevelChanged: DeepSharkAuvController.mapZoom = zoomLevel

                    MapPolyline {
                        line.width: 4
                        line.color: "#22d3ee"
                        path: root.missionPath()
                        visible: DeepSharkAuvController.showTrail && DeepSharkAuvController.waypointCount > 1
                    }

                    MapItemView {
                        model: DeepSharkAuvController.waypoints
                        delegate: MapQuickItem {
                            required property var modelData
                            coordinate: QtPositioning.coordinate(modelData.latitude, modelData.longitude)
                            anchorPoint.x: marker.width / 2
                            anchorPoint.y: marker.height / 2
                            sourceItem: Rectangle {
                                id: marker
                                width: 26; height: 26; radius: 13
                                color: "#0e7490"
                                border.color: "white"
                                border.width: 2
                                QGCLabel {
                                    anchors.centerIn: parent
                                    text: modelData.sequence
                                    color: "white"
                                    font.bold: true
                                }
                            }
                        }
                    }

                    MapQuickItem {
                        visible: root.displayCoordinate.isValid
                        coordinate: root.displayCoordinate
                        anchorPoint.x: vehicleMarker.width / 2
                        anchorPoint.y: vehicleMarker.height / 2
                        sourceItem: Item {
                            id: vehicleMarker
                            width: 44; height: 44
                            Rectangle {
                                anchors.centerIn: parent
                                width: 30; height: 30; radius: 15
                                color: DeepSharkAuvController.debugMode ? "#f59e0b" : "#16a34a"
                                border.color: "white"; border.width: 2
                                rotation: root.displayHeading
                                QGCLabel { anchors.centerIn: parent; text: "▲"; color: "white"; font.bold: true }
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 10
                        width: mapStatus.implicitWidth + 18
                        height: mapStatus.implicitHeight + 12
                        radius: 3
                        color: "#c0091118"
                        QGCLabel {
                            id: mapStatus
                            anchors.centerIn: parent
                            text: qsTr("怀柔水库  |  %1")
                                  .arg(DeepSharkAuvController.preferSatellite ? qsTr("卫星地图") : qsTr("跟随 QGC 地图"))
                            color: "white"
                            font.bold: true
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        spacing: 6
                        QGCButton { text: qsTr("怀柔"); onClicked: { DeepSharkAuvController.useHuairouReservoirPreset(); root.fitMission() } }
                        QGCButton { text: qsTr("适配任务"); enabled: DeepSharkAuvController.waypointCount > 0; onClicked: root.fitMission() }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: root.compact ? Math.max(175, root.width * 0.24) : Math.max(220, root.width * 0.21)
                Layout.fillHeight: true
                color: "#0b151d"
                border.color: "#263b49"
                radius: 4
                clip: true

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: ScreenTools.defaultFontPixelWidth * 0.75
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: parent.width
                        spacing: ScreenTools.defaultFontPixelHeight * 0.45

                        QGCLabel { text: qsTr("导航与载具状态"); color: "#f8fafc"; font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 10
                            QGCLabel { text: qsTr("数据源"); color: "#8fa5b3" }
                            QGCLabel {
                                text: DeepSharkAuvController.debugMode ? qsTr("本地调试") : (root.activeVehicle ? qsTr("已连接载具") : qsTr("未连接"))
                                color: DeepSharkAuvController.debugMode || root.activeVehicle ? "#4ade80" : "#f87171"
                                font.bold: true
                            }
                            QGCLabel { text: qsTr("深度"); color: "#8fa5b3" }
                            QGCLabel { text: root.displayDepth.toFixed(1) + " m"; color: "#e5edf3" }
                            QGCLabel { text: qsTr("速度"); color: "#8fa5b3" }
                            QGCLabel { text: root.displaySpeed.toFixed(2) + " m/s"; color: "#e5edf3" }
                            QGCLabel { text: qsTr("航向"); color: "#8fa5b3" }
                            QGCLabel { text: Math.round(root.displayHeading) + "°"; color: "#e5edf3" }
                            QGCLabel { text: qsTr("电量"); color: "#8fa5b3" }
                            QGCLabel {
                                text: DeepSharkAuvController.debugMode ? Math.round(root.debugBattery) + "%" : qsTr("--")
                                color: "#e5edf3"
                            }
                            QGCLabel { text: qsTr("任务计时"); color: "#8fa5b3" }
                            QGCLabel { text: root.formatElapsed(root.elapsedSeconds); color: "#e5edf3" }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#263b49" }
                        QGCLabel { text: qsTr("任务预览"); color: "#f8fafc"; font.bold: true }
                        QGCLabel {
                            Layout.fillWidth: true
                            text: DeepSharkAuvController.missionError.length > 0
                                  ? DeepSharkAuvController.missionError
                                  : DeepSharkAuvController.missionSummary.length > 0
                                    ? DeepSharkAuvController.missionSummary
                                    : qsTr("尚未导入 .plan 任务")
                            color: DeepSharkAuvController.missionError.length > 0 ? "#f87171" : "#9fb6c4"
                            wrapMode: Text.WordWrap
                        }
                        QGCLabel {
                            Layout.fillWidth: true
                            visible: DeepSharkAuvController.missionFile.length > 0
                            text: DeepSharkAuvController.missionFile
                            color: "#6f8b9b"
                            elide: Text.ElideMiddle
                        }
                        QGCButton {
                            text: qsTr("清除任务")
                            enabled: DeepSharkAuvController.waypointCount > 0
                            onClicked: {
                                DeepSharkAuvController.clearMission()
                                root.addEvent(qsTr("任务预览已清除"))
                            }
                        }
                        QGCButton {
                            text: qsTr("恢复测试任务")
                            onClicked: {
                                DeepSharkAuvController.loadTestMission()
                                root.fitMission()
                                root.addEvent(qsTr("已恢复主坝近岸巡检任务"))
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: "#263b49" }
                        QGCLabel { text: qsTr("最近事件"); color: "#f8fafc"; font.bold: true }
                        Repeater {
                            model: root.eventRows
                            QGCLabel {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData
                                color: "#8fa5b3"
                                font.pointSize: ScreenTools.smallFontPointSize
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: settingsOverlay
        anchors.fill: parent
        visible: false
        z: 50
        color: "#b0000000"
        MouseArea { anchors.fill: parent }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, ScreenTools.defaultFontPixelWidth * 76)
            height: Math.min(parent.height - 24, settingsContent.implicitHeight + 32)
            color: "#111a23"
            border.color: "#526174"
            border.width: 1
            radius: 5

            ColumnLayout {
                id: settingsContent
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    QGCLabel {
                        Layout.fillWidth: true
                        text: qsTr("AUV 工作区设置")
                        color: "#f8fafc"
                        font.bold: true
                        font.pointSize: ScreenTools.mediumFontPointSize
                    }
                    QGCButton { text: qsTr("关闭"); onClicked: settingsOverlay.visible = false }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#334155" }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 9

                    QGCLabel { text: qsTr("地图显示"); color: "#cbd5e1" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("优先卫星地图"), qsTr("跟随 QGC 地图设置")]
                        currentIndex: DeepSharkAuvController.preferSatellite ? 0 : 1
                        onActivated: DeepSharkAuvController.preferSatellite = currentIndex === 0
                    }
                    QGCLabel { text: qsTr("默认深度"); color: "#cbd5e1" }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0; to: 1000
                        value: Math.round(DeepSharkAuvController.defaultDepth * 10)
                        editable: true
                        textFromValue: function(value) { return (value / 10).toFixed(1) + " m" }
                        valueFromText: function(text) { return Math.round(parseFloat(text) * 10) }
                        onValueModified: DeepSharkAuvController.defaultDepth = value / 10
                    }
                    QGCLabel { text: qsTr("默认速度"); color: "#cbd5e1" }
                    SpinBox {
                        Layout.fillWidth: true
                        from: 0; to: 100
                        value: Math.round(DeepSharkAuvController.defaultSpeed * 10)
                        editable: true
                        textFromValue: function(value) { return (value / 10).toFixed(1) + " m/s" }
                        valueFromText: function(text) { return Math.round(parseFloat(text) * 10) }
                        onValueModified: DeepSharkAuvController.defaultSpeed = value / 10
                    }
                    QGCLabel { text: qsTr("跟随载具"); color: "#cbd5e1" }
                    Switch {
                        checked: DeepSharkAuvController.followVehicle
                        onToggled: DeepSharkAuvController.followVehicle = checked
                    }
                    QGCLabel { text: qsTr("显示任务航线"); color: "#cbd5e1" }
                    Switch {
                        checked: DeepSharkAuvController.showTrail
                        onToggled: DeepSharkAuvController.showTrail = checked
                    }
                }

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("地图中心、缩放、深度、速度和显示选项会自动保存。导入任务仅用于本界面预览，不会上传到载具。")
                    color: "#94a3b8"
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    QGCButton {
                        text: qsTr("怀柔水库预设")
                        onClicked: {
                            DeepSharkAuvController.useHuairouReservoirPreset()
                            root.fitMission()
                        }
                    }
                    QGCButton {
                        text: qsTr("恢复默认")
                        onClicked: {
                            DeepSharkAuvController.resetSettings()
                            root.fitMission()
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        debugPosition = routeStartCoordinate()
        addEvent(qsTr("AUV 任务工作区已就绪"))
        Qt.callLater(root.fitMission)
    }
}
