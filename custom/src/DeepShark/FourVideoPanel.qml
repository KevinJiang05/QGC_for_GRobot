/****************************************************************************
 *
 * DeepShark multi-layout video panel.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts

import DeepShark 1.0
import QGroundControl
import QGroundControl.Controls

Rectangle {
    id: root
    objectName: "deepSharkFourVideoPanel"

    property bool videoMainMode: true
    property string layoutMode: "grid"
    property string previousLayoutMode: "grid"
    property int selectedIndex: 0
    property int mainIndex: 0
    property int fullscreenIndex: -1
    property bool attitudeMode: false
    property bool auvMissionMode: false
    property string mainViewName: titleForIndex(mainIndex)
    readonly property var videoRows: [videoRow1, videoRow2, videoRow3, videoRow4]

    component VideoStatusRow: QtObject {
        required property int index
        required property var tile
        readonly property string name: root.titleForIndex(index - 1)
        readonly property string url: root.urlForIndex(index - 1)
        readonly property bool enabled: root.enabledForIndex(index - 1)
        readonly property string status: tile.currentStatus
        readonly property int retry: tile.retryCount
        readonly property bool streaming: tile.streaming
        readonly property bool decoding: tile.decoding
        readonly property int streamCount: tile.streamCount
        readonly property int decodeCount: tile.decodeCount
        readonly property string fps: tile.frameRateText
        readonly property string latency: tile.latencyText
        readonly property int estimatedLatencyMs: tile.estimatedLatencyMs
        readonly property string lastError: tile.lastError
        readonly property string watchdog: tile.watchdogStatus
        readonly property int lastProgressAge: tile.lastProgressAgeSeconds
        readonly property int watchdogReconnectCount: tile.watchdogReconnectCount
    }

    VideoStatusRow { id: videoRow1; index: 1; tile: videoTile1 }
    VideoStatusRow { id: videoRow2; index: 2; tile: videoTile2 }
    VideoStatusRow { id: videoRow3; index: 3; tile: videoTile3 }
    VideoStatusRow { id: videoRow4; index: 4; tile: videoTile4 }
    property var tileOrder: [0, 1, 2, 3]
    property bool tileDragging: false
    property int draggedTileIndex: -1
    property real dragCenterX: 0
    property real dragCenterY: 0
    property real dragOffsetX: 0
    property real dragOffsetY: 0
    readonly property var camera1PreviewItem: videoTile1.previewItem
    readonly property bool camera1Decoding: videoTile1.decoding
    readonly property int camera1VideoWidth: videoTile1.videoWidth
    readonly property int camera1VideoHeight: videoTile1.videoHeight
    readonly property string camera1Status: videoTile1.currentStatus
    property int reconnectAllIndex: 0
    property real _panelGap: Math.max(1, ScreenTools.defaultFontPixelWidth)
    property real _toolbarHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.2, 32)
    property bool _compactToolbar: width < ScreenTools.defaultFontPixelWidth * 112
    property bool _veryCompactToolbar: width < ScreenTools.defaultFontPixelWidth * 76

    signal toggleVideoMainMode()
    signal toggleStatusPanel()
    signal minimizePanel()
    signal deepSharkEvent(string message)

    radius: 4
    color: videoMainMode ? "#f2070b10" : "#10151c"
    border.color: "#384453"
    border.width: 1
    opacity: 0.96
    clip: true

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    Timer {
        id: reconnectAllTimer
        interval: 400
        repeat: true
        onTriggered: {
            while (root.reconnectAllIndex < 4 && !root.enabledForIndex(root.reconnectAllIndex)) {
                root.deepSharkEvent(qsTr("%1：通道已停用，跳过重连").arg(root.titleForIndex(root.reconnectAllIndex)))
                root.reconnectAllIndex++
            }
            if (root.reconnectAllIndex >= 4) {
                stop()
                return
            }
            root.reconnectVideo(root.reconnectAllIndex)
            root.reconnectAllIndex++
        }
    }

    function titleForIndex(index) {
        if (index === 0) return DeepSharkVideoSettings.camera1Name
        if (index === 1) return DeepSharkVideoSettings.camera2Name
        if (index === 2) return DeepSharkVideoSettings.camera3Name
        return DeepSharkVideoSettings.camera4Name
    }

    function urlForIndex(index) {
        if (index === 0) return DeepSharkVideoSettings.camera1Url
        if (index === 1) return DeepSharkVideoSettings.camera2Url
        if (index === 2) return DeepSharkVideoSettings.camera3Url
        return DeepSharkVideoSettings.camera4Url
    }

    function enabledForIndex(index) {
        if (index === 0) return DeepSharkVideoSettings.camera1Enabled
        if (index === 1) return DeepSharkVideoSettings.camera2Enabled
        if (index === 2) return DeepSharkVideoSettings.camera3Enabled
        return DeepSharkVideoSettings.camera4Enabled
    }

    function tileForIndex(index) {
        if (index === 0) return videoTile1
        if (index === 1) return videoTile2
        if (index === 2) return videoTile3
        return videoTile4
    }

    function orderSlot(index) {
        for (var i = 0; i < tileOrder.length; i++) {
            if (tileOrder[i] === index) {
                return i
            }
        }
        return index
    }

    function startVideo(index) {
        if (!enabledForIndex(index)) {
            deepSharkEvent(qsTr("%1：通道已停用").arg(titleForIndex(index)))
            return
        }
        if (urlForIndex(index).length === 0) {
            deepSharkEvent(qsTr("%1：RTSP URL 为空，无法启动").arg(titleForIndex(index)))
            return
        }
        tileForIndex(index).startVideo()
    }

    function stopVideo(index) {
        tileForIndex(index).stopVideo()
    }

    function reconnectVideo(index) {
        if (!enabledForIndex(index)) {
            deepSharkEvent(qsTr("%1：通道已停用，跳过重连").arg(titleForIndex(index)))
            return
        }
        if (urlForIndex(index).length === 0) {
            deepSharkEvent(qsTr("%1：RTSP URL 为空，无法重连").arg(titleForIndex(index)))
            return
        }
        tileForIndex(index).reconnectVideo()
    }

    function reconnectAllVideos() {
        deepSharkEvent(qsTr("已请求全部视频重连"))
        reconnectAllTimer.stop()
        reconnectAllIndex = 0
        reconnectAllTimer.start()
    }

    function selectTile(index) {
        if (selectedIndex !== index) {
            deepSharkEvent(qsTr("已选择 %1").arg(titleForIndex(index)))
        }
        selectedIndex = index
        if (layoutMode === "mainAux" || layoutMode === "panorama") {
            if (mainIndex !== index) {
                deepSharkEvent(qsTr("主画面已切换为 %1").arg(titleForIndex(index)))
            }
            mainIndex = index
        }
    }

    function setGridMode() {
        attitudeMode = false
        auvMissionMode = false
        if (layoutMode !== "grid") {
            deepSharkEvent(qsTr("布局已切换为四宫格"))
        }
        previousLayoutMode = layoutMode === "fullscreen" ? previousLayoutMode : layoutMode
        layoutMode = "grid"
        fullscreenIndex = -1
    }

    function setMainAuxMode(index) {
        attitudeMode = false
        auvMissionMode = false
        if (layoutMode !== "mainAux") {
            deepSharkEvent(qsTr("布局已切换为主辅画面"))
        }
        if (mainIndex !== index) {
            deepSharkEvent(qsTr("主画面已切换为 %1").arg(titleForIndex(index)))
        }
        selectedIndex = index
        mainIndex = index
        previousLayoutMode = "mainAux"
        layoutMode = "mainAux"
        fullscreenIndex = -1
    }

    function setPanoramaMode(index) {
        attitudeMode = false
        auvMissionMode = false
        if (layoutMode !== "panorama") {
            deepSharkEvent(qsTr("布局已切换为全景"))
        }
        if (mainIndex !== index) {
            deepSharkEvent(qsTr("主画面已切换为 %1").arg(titleForIndex(index)))
        }
        selectedIndex = index
        mainIndex = index
        previousLayoutMode = "panorama"
        layoutMode = "panorama"
        fullscreenIndex = -1
    }

    function setFullscreenMode(index) {
        attitudeMode = false
        auvMissionMode = false
        if (layoutMode !== "fullscreen" || fullscreenIndex !== index) {
            deepSharkEvent(qsTr("%1 已进入全屏").arg(titleForIndex(index)))
        }
        selectedIndex = index
        fullscreenIndex = index
        previousLayoutMode = layoutMode === "fullscreen" ? previousLayoutMode : layoutMode
        layoutMode = "fullscreen"
    }

    function exitFullscreen() {
        deepSharkEvent(qsTr("已退出全屏"))
        layoutMode = previousLayoutMode === "grid" || previousLayoutMode === "mainAux" || previousLayoutMode === "panorama"
                     ? previousLayoutMode : "grid"
        fullscreenIndex = -1
    }

    function handleDoubleClick(index) {
        selectedIndex = index
        if (layoutMode === "fullscreen") {
            exitFullscreen()
        } else if (layoutMode === "panorama") {
            if (index === mainIndex) {
                setFullscreenMode(index)
            } else {
                setPanoramaMode(index)
            }
        } else if (layoutMode === "mainAux" && index === mainIndex) {
            setFullscreenMode(index)
        } else {
            setMainAuxMode(index)
        }
    }

    function isTileVisible(index) {
        return !attitudeMode && !auvMissionMode && (layoutMode !== "fullscreen" || fullscreenIndex === index)
    }

    function toggleAttitudeMode() {
        attitudeMode = true
        auvMissionMode = false
        deepSharkEvent(qsTr("已打开 3D 姿态工作区"))
    }

    function toggleAuvMissionMode() {
        auvMissionMode = true
        attitudeMode = false
        deepSharkEvent(qsTr("已打开 AUV 任务工作区"))
    }

    function showVideoWorkspace() {
        attitudeMode = false
        auvMissionMode = false
        deepSharkEvent(qsTr("已打开视频工作区"))
    }

    function tileZ(index) {
        if (tileDragging && draggedTileIndex === index) return 20
        if (layoutMode === "fullscreen" && fullscreenIndex === index) return 4
        if ((layoutMode === "mainAux" || layoutMode === "panorama") && mainIndex === index) return 3
        return 1
    }

    function tileX(index) {
        var areaWidth = Math.max(1, videoArea.width)
        var gap = Math.min(_panelGap, Math.max(0, areaWidth * 0.08))
        if (layoutMode === "grid") {
            var gridSlot = orderSlot(index)
            return (gridSlot % 2) * ((areaWidth - gap) / 2 + gap)
        }
        if (layoutMode === "mainAux") {
            return index === mainIndex ? 0 : areaWidth * 0.74 + gap
        }
        if (layoutMode === "panorama") {
            return index === mainIndex ? 0 : panoramaSlot(index) * ((areaWidth - gap * 2) / 3 + gap)
        }
        return 0
    }

    function tileY(index) {
        var areaHeight = Math.max(1, videoArea.height)
        var gap = Math.min(_panelGap, Math.max(0, areaHeight * 0.08))
        if (layoutMode === "grid") {
            var gridSlot = orderSlot(index)
            return Math.floor(gridSlot / 2) * ((areaHeight - gap) / 2 + gap)
        }
        if (layoutMode === "mainAux") {
            return index === mainIndex ? 0 : auxSlot(index) * ((areaHeight - gap * 2) / 3 + gap)
        }
        if (layoutMode === "panorama") {
            return index === mainIndex ? 0 : (areaHeight - gap) / 2 + gap
        }
        return 0
    }

    function tileWidth(index) {
        var areaWidth = Math.max(1, videoArea.width)
        var gap = Math.min(_panelGap, Math.max(0, areaWidth * 0.08))
        if (layoutMode === "grid") return Math.max(1, (areaWidth - gap) / 2)
        if (layoutMode === "mainAux") return Math.max(1, index === mainIndex ? areaWidth * 0.74 : areaWidth * 0.26 - gap)
        if (layoutMode === "panorama") return Math.max(1, index === mainIndex ? areaWidth : (areaWidth - gap * 2) / 3)
        return areaWidth
    }

    function tileHeight(index) {
        var areaHeight = Math.max(1, videoArea.height)
        var gap = Math.min(_panelGap, Math.max(0, areaHeight * 0.08))
        if (layoutMode === "grid") return Math.max(1, (areaHeight - gap) / 2)
        if (layoutMode === "mainAux") return Math.max(1, index === mainIndex ? areaHeight : (areaHeight - gap * 2) / 3)
        if (layoutMode === "panorama") return Math.max(1, (areaHeight - gap) / 2)
        return areaHeight
    }

    function auxSlot(index) {
        var slot = 0
        for (var i = 0; i < tileOrder.length; i++) {
            var orderedIndex = tileOrder[i]
            if (orderedIndex === mainIndex) continue
            if (orderedIndex === index) return slot
            slot++
        }
        return 0
    }

    function panoramaSlot(index) {
        return auxSlot(index)
    }

    function tileAtPoint(pointX, pointY, skipIndex) {
        for (var i = 0; i < 4; i++) {
            if (i === skipIndex || !isTileVisible(i)) {
                continue
            }
            var x = tileX(i)
            var y = tileY(i)
            var w = tileWidth(i)
            var h = tileHeight(i)
            if (pointX >= x && pointX <= x + w && pointY >= y && pointY <= y + h) {
                return i
            }
        }
        return -1
    }

    function swapTileOrder(firstIndex, secondIndex) {
        if (firstIndex < 0 || secondIndex < 0 || firstIndex === secondIndex) {
            return
        }
        var firstSlot = orderSlot(firstIndex)
        var secondSlot = orderSlot(secondIndex)
        var updatedOrder = tileOrder.slice(0)
        updatedOrder[firstSlot] = secondIndex
        updatedOrder[secondSlot] = firstIndex
        tileOrder = updatedOrder
        if (mainIndex === firstIndex) {
            mainIndex = secondIndex
        } else if (mainIndex === secondIndex) {
            mainIndex = firstIndex
        }
        if (selectedIndex === firstIndex) {
            selectedIndex = secondIndex
        } else if (selectedIndex === secondIndex) {
            selectedIndex = firstIndex
        }
        deepSharkEvent(qsTr("已交换 %1 与 %2").arg(titleForIndex(firstIndex)).arg(titleForIndex(secondIndex)))
    }

    function beginTileDrag(index, centerX, centerY) {
        if (attitudeMode || auvMissionMode || layoutMode === "fullscreen") {
            return
        }
        draggedTileIndex = index
        dragCenterX = centerX
        dragCenterY = centerY
        dragOffsetX = centerX - tileX(index)
        dragOffsetY = centerY - tileY(index)
        tileDragging = true
    }

    function updateTileDrag(index, centerX, centerY) {
        if (!tileDragging || draggedTileIndex !== index) {
            return
        }
        dragCenterX = centerX
        dragCenterY = centerY
    }

    function finishTileDrag(index, centerX, centerY) {
        if (!tileDragging || draggedTileIndex !== index) {
            return
        }
        var targetIndex = tileAtPoint(centerX, centerY, index)
        tileDragging = false
        draggedTileIndex = -1
        if (targetIndex >= 0) {
            swapTileOrder(index, targetIndex)
        }
    }

    function openSettings() {
        rtspTransportCombo.currentIndex = DeepSharkVideoSettings.rtspTransport
        camera1EnabledCheck.checked = DeepSharkVideoSettings.camera1Enabled
        camera1NameField.text = DeepSharkVideoSettings.camera1Name
        camera1UrlField.text = DeepSharkVideoSettings.camera1Url
        camera2EnabledCheck.checked = DeepSharkVideoSettings.camera2Enabled
        camera2NameField.text = DeepSharkVideoSettings.camera2Name
        camera2UrlField.text = DeepSharkVideoSettings.camera2Url
        camera3EnabledCheck.checked = DeepSharkVideoSettings.camera3Enabled
        camera3NameField.text = DeepSharkVideoSettings.camera3Name
        camera3UrlField.text = DeepSharkVideoSettings.camera3Url
        camera4EnabledCheck.checked = DeepSharkVideoSettings.camera4Enabled
        camera4NameField.text = DeepSharkVideoSettings.camera4Name
        camera4UrlField.text = DeepSharkVideoSettings.camera4Url
        settingsOverlay.visible = true
    }

    function saveSettings() {
        var oldUrls = [DeepSharkVideoSettings.camera1Url, DeepSharkVideoSettings.camera2Url, DeepSharkVideoSettings.camera3Url, DeepSharkVideoSettings.camera4Url]
        var newUrls = [camera1UrlField.text, camera2UrlField.text, camera3UrlField.text, camera4UrlField.text]
        var oldEnabled = [DeepSharkVideoSettings.camera1Enabled, DeepSharkVideoSettings.camera2Enabled, DeepSharkVideoSettings.camera3Enabled, DeepSharkVideoSettings.camera4Enabled]
        var newEnabled = [camera1EnabledCheck.checked, camera2EnabledCheck.checked, camera3EnabledCheck.checked, camera4EnabledCheck.checked]

        DeepSharkVideoSettings.setCamera(1, camera1NameField.text, camera1UrlField.text)
        DeepSharkVideoSettings.setCameraEnabled(1, camera1EnabledCheck.checked)
        DeepSharkVideoSettings.setCamera(2, camera2NameField.text, camera2UrlField.text)
        DeepSharkVideoSettings.setCameraEnabled(2, camera2EnabledCheck.checked)
        DeepSharkVideoSettings.setCamera(3, camera3NameField.text, camera3UrlField.text)
        DeepSharkVideoSettings.setCameraEnabled(3, camera3EnabledCheck.checked)
        DeepSharkVideoSettings.setCamera(4, camera4NameField.text, camera4UrlField.text)
        DeepSharkVideoSettings.setCameraEnabled(4, camera4EnabledCheck.checked)
        DeepSharkVideoSettings.rtspTransport = rtspTransportCombo.currentIndex

        for (var i = 0; i < 4; i++) {
            if (!newEnabled[i]) {
                if (oldEnabled[i]) {
                    deepSharkEvent(qsTr("%1：通道已停用").arg(titleForIndex(i)))
                }
            } else if (!oldEnabled[i] || oldUrls[i] !== newUrls[i]) {
                deepSharkEvent(!oldEnabled[i]
                               ? qsTr("%1：通道已启用").arg(titleForIndex(i))
                               : qsTr("视频 %1：URL 已更新").arg(i + 1))
                reconnectVideo(i)
            }
        }

        settingsOverlay.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelWidth

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: _toolbarHeight
            spacing: ScreenTools.defaultFontPixelWidth * 0.7

            QGCLabel {
                Layout.fillWidth: false
                Layout.minimumWidth: root._veryCompactToolbar ? ScreenTools.defaultFontPixelWidth * 10 : ScreenTools.defaultFontPixelWidth * 18
                Layout.preferredWidth: root._veryCompactToolbar ? ScreenTools.defaultFontPixelWidth * 14 : ScreenTools.defaultFontPixelWidth * 28
                Layout.maximumWidth: root._compactToolbar ? ScreenTools.defaultFontPixelWidth * 28 : ScreenTools.defaultFontPixelWidth * 42
                text: auvMissionMode
                      ? qsTr("DeepShark AUV 任务")
                      : attitudeMode
                      ? qsTr("DeepShark 3D 姿态")
                      : layoutMode === "fullscreen"
                      ? qsTr("DeepShark 视频面板 - 全屏")
                      : layoutMode === "mainAux"
                        ? qsTr("DeepShark 视频面板 - 主辅")
                        : layoutMode === "panorama"
                          ? qsTr("DeepShark 视频面板 - 全景")
                          : qsTr("DeepShark 视频面板")
                color: "#f2f5f8"
                font.bold: true
                elide: Text.ElideRight
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
            }

            Flickable {
                Layout.preferredWidth: Math.min(toolbarActions.implicitWidth, Math.max(1, root.width - ScreenTools.defaultFontPixelWidth * (root._veryCompactToolbar ? 34 : 54)))
                Layout.preferredHeight: _toolbarHeight
                contentWidth: toolbarActions.implicitWidth
                contentHeight: _toolbarHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentWidth > width

                RowLayout {
                    id: toolbarActions
                    height: parent.height
                    spacing: ScreenTools.defaultFontPixelWidth * 0.7

                    QGCButton {
                        text: qsTr("视频")
                        backgroundColor: !root.attitudeMode && !root.auvMissionMode ? "#0e7490" : "#334155"
                        textColor: "#f8fafc"
                        showBorder: true
                        onClicked: root.showVideoWorkspace()
                    }
                    QGCButton {
                        text: qsTr("3D 姿态")
                        backgroundColor: root.attitudeMode ? "#0e7490" : "#334155"
                        textColor: "#f8fafc"
                        showBorder: true
                        onClicked: root.toggleAttitudeMode()
                    }
                    QGCButton {
                        text: qsTr("AUV 任务")
                        backgroundColor: root.auvMissionMode ? "#0e7490" : "#334155"
                        textColor: "#f8fafc"
                        showBorder: true
                        onClicked: root.toggleAuvMissionMode()
                    }
                    QGCButton {
                        text: qsTr("载具状态")
                        backgroundColor: "#334155"
                        textColor: "#f8fafc"
                        showBorder: true
                        onClicked: root.toggleStatusPanel()
                    }
                    Rectangle {
                        visible: !root.attitudeMode && !root.auvMissionMode
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        Layout.topMargin: 5
                        Layout.bottomMargin: 5
                        color: "#526174"
                    }
                    QGCButton { visible: !root.attitudeMode && !root.auvMissionMode; text: qsTr("四宫格"); onClicked: root.setGridMode() }
                    QGCButton { visible: !root.attitudeMode && !root.auvMissionMode; text: qsTr("主辅"); onClicked: root.setMainAuxMode(root.selectedIndex) }
                    QGCButton { visible: !root.attitudeMode && !root.auvMissionMode; text: qsTr("全景"); onClicked: root.setPanoramaMode(root.selectedIndex) }
                    QGCButton { visible: !root.attitudeMode && !root.auvMissionMode; text: layoutMode === "fullscreen" ? qsTr("退出全屏") : qsTr("全屏"); onClicked: layoutMode === "fullscreen" ? root.exitFullscreen() : root.setFullscreenMode(root.selectedIndex) }
                    QGCButton {
                        visible: !root.attitudeMode && !root.auvMissionMode
                        text: qsTr("启动")
                        backgroundColor: "#16a34a"
                        textColor: "#f8fafc"
                        showBorder: true
                        onClicked: root.startVideo(root.selectedIndex)
                    }
                    QGCButton {
                        visible: !root.attitudeMode && !root.auvMissionMode
                        text: qsTr("停止")
                        backgroundColor: "#dc2626"
                        textColor: "#fef2f2"
                        showBorder: true
                        onClicked: root.stopVideo(root.selectedIndex)
                    }
                    QGCButton {
                        visible: !root.attitudeMode && !root.auvMissionMode
                        text: qsTr("重连")
                        backgroundColor: "#059669"
                        textColor: "#ecfdf5"
                        showBorder: true
                        onClicked: root.reconnectVideo(root.selectedIndex)
                    }
                    QGCButton {
                        visible: !root.attitudeMode && !root.auvMissionMode
                        text: qsTr("全部重连")
                        backgroundColor: "#86efac"
                        textColor: "#064e3b"
                        showBorder: true
                        onClicked: root.reconnectAllVideos()
                    }
                }
            }

            QGCButton { text: root._veryCompactToolbar ? qsTr("最小") : qsTr("最小化"); onClicked: root.minimizePanel() }
            QGCButton {
                visible: !root._veryCompactToolbar
                text: root.videoMainMode ? qsTr("显示地图") : qsTr("主视频")
                onClicked: root.toggleVideoMainMode()
            }
            QGCButton {
                visible: !root.auvMissionMode
                text: root._veryCompactToolbar ? qsTr("设") : qsTr("设置")
                backgroundColor: "#facc15"
                textColor: "#422006"
                showBorder: true
                onClicked: root.openSettings()
            }
        }

        Item {
            id: videoArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 1
            clip: true

            Loader {
                id: attitudePanelLoader
                anchors.fill: parent
                active: root.visible && root.attitudeMode
                visible: active
                z: 8
                sourceComponent: Component {
                    Attitude3DPanel {}
                }
            }

            Loader {
                anchors.fill: parent
                active: root.auvMissionMode
                visible: active
                z: 9
                source: "AuvMissionPanel.qml"
            }

            VideoTile {
                id: videoTile1
                x: root.tileDragging && root.draggedTileIndex === 0 ? root.dragCenterX - root.dragOffsetX : root.tileX(0)
                y: root.tileDragging && root.draggedTileIndex === 0 ? root.dragCenterY - root.dragOffsetY : root.tileY(0)
                width: root.tileWidth(0); height: root.tileHeight(0)
                // Keep the first source renderable while another workspace covers it,
                // so the right-side preview can mirror the existing video texture.
                z: root.tileZ(0); visible: true
                title: root.titleForIndex(0); receiverName: "deepSharkVideo1"
                selected: root.selectedIndex === 0
                videoEnabled: root.enabledForIndex(0) && root.urlForIndex(0).length > 0
                videoSource: root.urlForIndex(0)
                onTileClicked: root.selectTile(0)
                onTileDoubleClicked: root.handleDoubleClick(0)
                onTileDragStarted: function(centerX, centerY) { root.beginTileDrag(0, centerX, centerY) }
                onTileDragMoved: function(centerX, centerY) { root.updateTileDrag(0, centerX, centerY) }
                onTileDragReleased: function(centerX, centerY) { root.finishTileDrag(0, centerX, centerY) }
                onVideoEvent: function(message) { root.deepSharkEvent(message) }
            }

            VideoTile {
                id: videoTile2
                x: root.tileDragging && root.draggedTileIndex === 1 ? root.dragCenterX - root.dragOffsetX : root.tileX(1)
                y: root.tileDragging && root.draggedTileIndex === 1 ? root.dragCenterY - root.dragOffsetY : root.tileY(1)
                width: root.tileWidth(1); height: root.tileHeight(1)
                z: root.tileZ(1); visible: root.isTileVisible(1)
                title: root.titleForIndex(1); receiverName: "deepSharkVideo2"
                selected: root.selectedIndex === 1
                videoEnabled: root.enabledForIndex(1) && root.urlForIndex(1).length > 0
                videoSource: root.urlForIndex(1)
                onTileClicked: root.selectTile(1)
                onTileDoubleClicked: root.handleDoubleClick(1)
                onTileDragStarted: function(centerX, centerY) { root.beginTileDrag(1, centerX, centerY) }
                onTileDragMoved: function(centerX, centerY) { root.updateTileDrag(1, centerX, centerY) }
                onTileDragReleased: function(centerX, centerY) { root.finishTileDrag(1, centerX, centerY) }
                onVideoEvent: function(message) { root.deepSharkEvent(message) }
            }

            VideoTile {
                id: videoTile3
                x: root.tileDragging && root.draggedTileIndex === 2 ? root.dragCenterX - root.dragOffsetX : root.tileX(2)
                y: root.tileDragging && root.draggedTileIndex === 2 ? root.dragCenterY - root.dragOffsetY : root.tileY(2)
                width: root.tileWidth(2); height: root.tileHeight(2)
                z: root.tileZ(2); visible: root.isTileVisible(2)
                title: root.titleForIndex(2); receiverName: "deepSharkVideo3"
                selected: root.selectedIndex === 2
                videoEnabled: root.enabledForIndex(2) && root.urlForIndex(2).length > 0
                videoSource: root.urlForIndex(2)
                onTileClicked: root.selectTile(2)
                onTileDoubleClicked: root.handleDoubleClick(2)
                onTileDragStarted: function(centerX, centerY) { root.beginTileDrag(2, centerX, centerY) }
                onTileDragMoved: function(centerX, centerY) { root.updateTileDrag(2, centerX, centerY) }
                onTileDragReleased: function(centerX, centerY) { root.finishTileDrag(2, centerX, centerY) }
                onVideoEvent: function(message) { root.deepSharkEvent(message) }
            }

            VideoTile {
                id: videoTile4
                x: root.tileDragging && root.draggedTileIndex === 3 ? root.dragCenterX - root.dragOffsetX : root.tileX(3)
                y: root.tileDragging && root.draggedTileIndex === 3 ? root.dragCenterY - root.dragOffsetY : root.tileY(3)
                width: root.tileWidth(3); height: root.tileHeight(3)
                z: root.tileZ(3); visible: root.isTileVisible(3)
                title: root.titleForIndex(3); receiverName: "deepSharkVideo4"
                selected: root.selectedIndex === 3
                videoEnabled: root.enabledForIndex(3) && root.urlForIndex(3).length > 0
                videoSource: root.urlForIndex(3)
                onTileClicked: root.selectTile(3)
                onTileDoubleClicked: root.handleDoubleClick(3)
                onTileDragStarted: function(centerX, centerY) { root.beginTileDrag(3, centerX, centerY) }
                onTileDragMoved: function(centerX, centerY) { root.updateTileDrag(3, centerX, centerY) }
                onTileDragReleased: function(centerX, centerY) { root.finishTileDrag(3, centerX, centerY) }
                onVideoEvent: function(message) { root.deepSharkEvent(message) }
            }
        }
    }

    Rectangle {
        id: settingsOverlay
        anchors.fill: parent
        visible: false
        z: 10
        color: "#b0070b10"

        MouseArea { anchors.fill: parent }

        Rectangle {
            id: settingsCard
            anchors.centerIn: parent
            width: Math.max(1, Math.min(parent.width - ScreenTools.defaultFontPixelWidth, ScreenTools.defaultFontPixelWidth * 132))
            height: Math.max(1, Math.min(parent.height - ScreenTools.defaultFontPixelWidth * 4, settingsContent.implicitHeight + ScreenTools.defaultFontPixelWidth * 4))
            radius: 6
            color: "#111821"
            border.color: "#526174"
            border.width: 1
            clip: true

            ColumnLayout {
                id: settingsContent
                anchors.fill: parent
                anchors.margins: ScreenTools.defaultFontPixelWidth * 2
                spacing: ScreenTools.defaultFontPixelWidth * 1.2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        Layout.fillWidth: true
                        text: qsTr("DeepShark 视频设置")
                        color: "#f2f5f8"
                        font.bold: true
                        font.pointSize: ScreenTools.mediumFontPointSize
                    }

                    QGCButton { text: qsTr("取消"); onClicked: settingsOverlay.visible = false }
                    QGCButton { text: qsTr("保存"); onClicked: root.saveSettings() }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#2b3542"
                }

                QGCCheckBoxSlider {
                    Layout.fillWidth: true
                    text: qsTr("YOLO 检测叠加层")
                    onClicked: AIDetectionManager.overlayEnabled = checked

                    Binding on checked {
                        value: AIDetectionManager.overlayEnabled
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelWidth

                    QGCLabel { text: qsTr("视频传输方式"); color: "#cbd5e1" }
                    QGCComboBox {
                        id: rtspTransportCombo
                        objectName: "rtspTransportCombo"
                        Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 24
                        model: [qsTr("自动（按地址）"), qsTr("TCP（可靠传输）"), qsTr("UDP（低延迟）")]
                    }
                }

                QGCLabel {
                    Layout.fillWidth: true
                    text: qsTr("适用于全部 RTSP 通道。选择 TCP 或 UDP 后无需修改地址；保存后自动重连正在播放的视频。")
                    color: "#cbd5e1"
                    wrapMode: Text.WordWrap
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: ScreenTools.defaultFontPixelWidth * 1.2
                    rowSpacing: ScreenTools.defaultFontPixelWidth * 0.8

                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 5; text: qsTr("通道"); color: "#cbd5e1"; font.bold: true }
                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 9; text: qsTr("名称"); color: "#cbd5e1"; font.bold: true }
                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 7; text: qsTr("启用"); color: "#cbd5e1"; font.bold: true }
                    QGCLabel { Layout.fillWidth: true; text: qsTr("RTSP URL"); color: "#cbd5e1"; font.bold: true }

                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 5; text: "1"; color: "#cbd5e1" }
                    QGCTextField { id: camera1NameField; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 9 }
                    QGCCheckBox { id: camera1EnabledCheck; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 7 }
                    QGCTextField { id: camera1UrlField; Layout.fillWidth: true }

                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 5; text: "2"; color: "#cbd5e1" }
                    QGCTextField { id: camera2NameField; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 9 }
                    QGCCheckBox { id: camera2EnabledCheck; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 7 }
                    QGCTextField { id: camera2UrlField; Layout.fillWidth: true }

                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 5; text: "3"; color: "#cbd5e1" }
                    QGCTextField { id: camera3NameField; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 9 }
                    QGCCheckBox { id: camera3EnabledCheck; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 7 }
                    QGCTextField { id: camera3UrlField; Layout.fillWidth: true }

                    QGCLabel { Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 5; text: "4"; color: "#cbd5e1" }
                    QGCTextField { id: camera4NameField; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 9 }
                    QGCCheckBox { id: camera4EnabledCheck; Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 7 }
                    QGCTextField { id: camera4UrlField; Layout.fillWidth: true }
                }
            }
        }
    }
}
