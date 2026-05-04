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
import QGroundControl.Palette
import QGroundControl.ScreenTools

Rectangle {
    id: root

    property bool videoMainMode: true
    property string layoutMode: "grid"
    property string previousLayoutMode: "grid"
    property int selectedIndex: 0
    property int mainIndex: 0
    property int fullscreenIndex: -1
    property string mainViewName: titleForIndex(mainIndex)
    property var videoRows: []
    property int reconnectAllIndex: 0

    signal toggleVideoMainMode()
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

    Component.onCompleted: refreshVideoRows()

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.refreshVideoRows()
    }

    Timer {
        id: reconnectAllTimer
        interval: 400
        repeat: true
        onTriggered: {
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

    function tileForIndex(index) {
        if (index === 0) return videoTile1
        if (index === 1) return videoTile2
        if (index === 2) return videoTile3
        return videoTile4
    }

    function startVideo(index) {
        tileForIndex(index).startVideo()
        refreshVideoRows()
    }

    function stopVideo(index) {
        tileForIndex(index).stopVideo()
        refreshVideoRows()
    }

    function reconnectVideo(index) {
        tileForIndex(index).reconnectVideo()
        refreshVideoRows()
    }

    function reconnectAllVideos() {
        deepSharkEvent("Reconnect All triggered")
        reconnectAllTimer.stop()
        reconnectAllIndex = 0
        reconnectAllTimer.start()
    }

    function selectTile(index) {
        if (selectedIndex !== index) {
            deepSharkEvent("Selected " + titleForIndex(index))
        }
        selectedIndex = index
        if (layoutMode === "mainAux") {
            if (mainIndex !== index) {
                deepSharkEvent("Main view switched to " + titleForIndex(index))
            }
            mainIndex = index
        }
    }

    function setGridMode() {
        if (layoutMode !== "grid") {
            deepSharkEvent("Layout switched to grid")
        }
        previousLayoutMode = layoutMode === "fullscreen" ? previousLayoutMode : layoutMode
        layoutMode = "grid"
        fullscreenIndex = -1
    }

    function setMainAuxMode(index) {
        if (layoutMode !== "mainAux") {
            deepSharkEvent("Layout switched to mainAux")
        }
        if (mainIndex !== index) {
            deepSharkEvent("Main view switched to " + titleForIndex(index))
        }
        selectedIndex = index
        mainIndex = index
        previousLayoutMode = "mainAux"
        layoutMode = "mainAux"
        fullscreenIndex = -1
    }

    function setFullscreenMode(index) {
        if (layoutMode !== "fullscreen" || fullscreenIndex !== index) {
            deepSharkEvent("Fullscreen entered: " + titleForIndex(index))
        }
        selectedIndex = index
        fullscreenIndex = index
        previousLayoutMode = layoutMode === "fullscreen" ? previousLayoutMode : layoutMode
        layoutMode = "fullscreen"
    }

    function exitFullscreen() {
        deepSharkEvent("Fullscreen exited")
        layoutMode = previousLayoutMode === "grid" ? "grid" : "mainAux"
        fullscreenIndex = -1
    }

    function handleDoubleClick(index) {
        selectedIndex = index
        if (layoutMode === "fullscreen") {
            exitFullscreen()
        } else if (layoutMode === "mainAux" && index === mainIndex) {
            setFullscreenMode(index)
        } else {
            setMainAuxMode(index)
        }
    }

    function isTileVisible(index) {
        return layoutMode !== "fullscreen" || fullscreenIndex === index
    }

    function tileZ(index) {
        if (layoutMode === "fullscreen" && fullscreenIndex === index) return 4
        if (layoutMode === "mainAux" && mainIndex === index) return 3
        return 1
    }

    function tileX(index) {
        var gap = ScreenTools.defaultFontPixelWidth
        if (layoutMode === "grid") {
            return (index % 2) * ((videoArea.width - gap) / 2 + gap)
        }
        if (layoutMode === "mainAux") {
            return index === mainIndex ? 0 : videoArea.width * 0.74 + gap
        }
        return 0
    }

    function tileY(index) {
        var gap = ScreenTools.defaultFontPixelWidth
        if (layoutMode === "grid") {
            return Math.floor(index / 2) * ((videoArea.height - gap) / 2 + gap)
        }
        if (layoutMode === "mainAux") {
            return index === mainIndex ? 0 : auxSlot(index) * ((videoArea.height - gap * 2) / 3 + gap)
        }
        return 0
    }

    function tileWidth(index) {
        var gap = ScreenTools.defaultFontPixelWidth
        if (layoutMode === "grid") return (videoArea.width - gap) / 2
        if (layoutMode === "mainAux") return index === mainIndex ? videoArea.width * 0.74 : videoArea.width * 0.26 - gap
        return videoArea.width
    }

    function tileHeight(index) {
        var gap = ScreenTools.defaultFontPixelWidth
        if (layoutMode === "grid") return (videoArea.height - gap) / 2
        if (layoutMode === "mainAux") return index === mainIndex ? videoArea.height : (videoArea.height - gap * 2) / 3
        return videoArea.height
    }

    function auxSlot(index) {
        var slot = 0
        for (var i = 0; i < 4; i++) {
            if (i === mainIndex) continue
            if (i === index) return slot
            slot++
        }
        return 0
    }

    function videoRow(index, tile) {
        return {
            "index": index + 1,
            "name": titleForIndex(index),
            "url": urlForIndex(index),
            "status": tile ? tile.currentStatus : "Unknown",
            "retry": tile ? tile.retryCount : 0,
            "streaming": tile ? tile.streaming : false,
            "decoding": tile ? tile.decoding : false,
            "streamCount": tile ? tile.streamCount : 0,
            "decodeCount": tile ? tile.decodeCount : 0,
            "fps": tile ? tile.frameRateText : "FPS: --",
            "latency": tile ? tile.latencyText : "Latency: --",
            "estimatedLatencyMs": tile ? tile.estimatedLatencyMs : -1,
            "lastError": tile ? tile.lastError : "",
            "watchdog": tile ? tile.watchdogStatus : "Disabled",
            "lastProgressAge": tile ? tile.lastProgressAgeSeconds : -1,
            "watchdogReconnectCount": tile ? tile.watchdogReconnectCount : 0
        }
    }

    function refreshVideoRows() {
        videoRows = [
            videoRow(0, videoTile1),
            videoRow(1, videoTile2),
            videoRow(2, videoTile3),
            videoRow(3, videoTile4)
        ]
    }

    function openSettings() {
        camera1NameField.text = DeepSharkVideoSettings.camera1Name
        camera1UrlField.text = DeepSharkVideoSettings.camera1Url
        camera2NameField.text = DeepSharkVideoSettings.camera2Name
        camera2UrlField.text = DeepSharkVideoSettings.camera2Url
        camera3NameField.text = DeepSharkVideoSettings.camera3Name
        camera3UrlField.text = DeepSharkVideoSettings.camera3Url
        camera4NameField.text = DeepSharkVideoSettings.camera4Name
        camera4UrlField.text = DeepSharkVideoSettings.camera4Url
        settingsOverlay.visible = true
    }

    function saveSettings() {
        var oldUrls = [DeepSharkVideoSettings.camera1Url, DeepSharkVideoSettings.camera2Url, DeepSharkVideoSettings.camera3Url, DeepSharkVideoSettings.camera4Url]
        var newUrls = [camera1UrlField.text, camera2UrlField.text, camera3UrlField.text, camera4UrlField.text]

        DeepSharkVideoSettings.setCamera(1, camera1NameField.text, camera1UrlField.text)
        DeepSharkVideoSettings.setCamera(2, camera2NameField.text, camera2UrlField.text)
        DeepSharkVideoSettings.setCamera(3, camera3NameField.text, camera3UrlField.text)
        DeepSharkVideoSettings.setCamera(4, camera4NameField.text, camera4UrlField.text)

        for (var i = 0; i < 4; i++) {
            if (oldUrls[i] !== newUrls[i]) {
                deepSharkEvent("URL changed: video" + (i + 1))
                reconnectVideo(i)
            }
        }

        refreshVideoRows()
        settingsOverlay.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ScreenTools.defaultFontPixelWidth
        spacing: ScreenTools.defaultFontPixelWidth

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.2, 32)
            spacing: ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth: true
                text: layoutMode === "fullscreen"
                      ? qsTr("DeepShark Video Panel - Fullscreen")
                      : (layoutMode === "mainAux" ? qsTr("DeepShark Video Panel - Main Aux") : qsTr("DeepShark Video Panel"))
                color: "#f2f5f8"
                font.bold: true
                elide: Text.ElideRight
            }

            QGCLabel {
                text: qsTr("By KevinJiang")
                color: "#94a3b8"
                font.pixelSize: ScreenTools.defaultFontPixelHeight * 0.85
            }

            QGCButton { text: qsTr("四宫格"); onClicked: root.setGridMode() }
            QGCButton { text: qsTr("主辅"); onClicked: root.setMainAuxMode(root.selectedIndex) }
            QGCButton { text: layoutMode === "fullscreen" ? qsTr("退出全屏") : qsTr("全屏"); onClicked: layoutMode === "fullscreen" ? root.exitFullscreen() : root.setFullscreenMode(root.selectedIndex) }
            QGCButton {
                text: qsTr("启动")
                backgroundColor: "#16a34a"
                textColor: "#f8fafc"
                showBorder: true
                onClicked: root.startVideo(root.selectedIndex)
            }
            QGCButton {
                text: qsTr("停止")
                backgroundColor: "#dc2626"
                textColor: "#fef2f2"
                showBorder: true
                onClicked: root.stopVideo(root.selectedIndex)
            }
            QGCButton {
                text: qsTr("重连")
                backgroundColor: "#059669"
                textColor: "#ecfdf5"
                showBorder: true
                onClicked: root.reconnectVideo(root.selectedIndex)
            }
            QGCButton {
                text: qsTr("全部重连")
                backgroundColor: "#86efac"
                textColor: "#064e3b"
                showBorder: true
                onClicked: root.reconnectAllVideos()
            }
            QGCButton {
                text: qsTr("设置")
                backgroundColor: "#facc15"
                textColor: "#422006"
                showBorder: true
                onClicked: root.openSettings()
            }
            QGCButton { text: qsTr("最小化"); onClicked: root.minimizePanel() }
            QGCButton { text: root.videoMainMode ? qsTr("显示地图") : qsTr("主视频"); onClicked: root.toggleVideoMainMode() }
        }

        Item {
            id: videoArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            VideoTile {
                id: videoTile1
                x: root.tileX(0); y: root.tileY(0); width: root.tileWidth(0); height: root.tileHeight(0)
                z: root.tileZ(0); visible: root.isTileVisible(0)
                title: root.titleForIndex(0); receiverName: "deepSharkVideo1"
                selected: root.selectedIndex === 0
                videoEnabled: root.urlForIndex(0).length > 0
                videoSource: root.urlForIndex(0)
                onTileClicked: root.selectTile(0)
                onTileDoubleClicked: root.handleDoubleClick(0)
                onVideoEvent: function(message) { root.deepSharkEvent(message); root.refreshVideoRows() }
            }

            VideoTile {
                id: videoTile2
                x: root.tileX(1); y: root.tileY(1); width: root.tileWidth(1); height: root.tileHeight(1)
                z: root.tileZ(1); visible: root.isTileVisible(1)
                title: root.titleForIndex(1); receiverName: "deepSharkVideo2"
                selected: root.selectedIndex === 1
                videoEnabled: root.urlForIndex(1).length > 0
                videoSource: root.urlForIndex(1)
                onTileClicked: root.selectTile(1)
                onTileDoubleClicked: root.handleDoubleClick(1)
                onVideoEvent: function(message) { root.deepSharkEvent(message); root.refreshVideoRows() }
            }

            VideoTile {
                id: videoTile3
                x: root.tileX(2); y: root.tileY(2); width: root.tileWidth(2); height: root.tileHeight(2)
                z: root.tileZ(2); visible: root.isTileVisible(2)
                title: root.titleForIndex(2); receiverName: "deepSharkVideo3"
                selected: root.selectedIndex === 2
                videoEnabled: root.urlForIndex(2).length > 0
                videoSource: root.urlForIndex(2)
                onTileClicked: root.selectTile(2)
                onTileDoubleClicked: root.handleDoubleClick(2)
                onVideoEvent: function(message) { root.deepSharkEvent(message); root.refreshVideoRows() }
            }

            VideoTile {
                id: videoTile4
                x: root.tileX(3); y: root.tileY(3); width: root.tileWidth(3); height: root.tileHeight(3)
                z: root.tileZ(3); visible: root.isTileVisible(3)
                title: root.titleForIndex(3); receiverName: "deepSharkVideo4"
                selected: root.selectedIndex === 3
                videoEnabled: root.urlForIndex(3).length > 0
                videoSource: root.urlForIndex(3)
                onTileClicked: root.selectTile(3)
                onTileDoubleClicked: root.handleDoubleClick(3)
                onVideoEvent: function(message) { root.deepSharkEvent(message); root.refreshVideoRows() }
            }
        }
    }

    Rectangle {
        id: settingsOverlay
        anchors.fill: parent
        visible: false
        z: 10
        color: "#d910151c"
        border.color: "#526174"
        border.width: 1
        radius: 4

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: ScreenTools.defaultFontPixelWidth * 1.5
            spacing: ScreenTools.defaultFontPixelWidth

            RowLayout {
                Layout.fillWidth: true
                QGCLabel { Layout.fillWidth: true; text: qsTr("DeepShark Video Settings"); color: "#f2f5f8"; font.bold: true }
                QGCButton { text: qsTr("取消"); onClicked: settingsOverlay.visible = false }
                QGCButton { text: qsTr("保存"); onClicked: root.saveSettings() }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 3
                columnSpacing: ScreenTools.defaultFontPixelWidth
                rowSpacing: ScreenTools.defaultFontPixelWidth * 0.7

                QGCLabel { text: qsTr("通道"); color: "#cbd5e1"; font.bold: true }
                QGCLabel { text: qsTr("名称"); color: "#cbd5e1"; font.bold: true }
                QGCLabel { text: qsTr("RTSP URL"); color: "#cbd5e1"; font.bold: true }

                QGCLabel { text: "1"; color: "#cbd5e1" }
                QGCTextField { id: camera1NameField; Layout.fillWidth: true }
                QGCTextField { id: camera1UrlField; Layout.fillWidth: true }

                QGCLabel { text: "2"; color: "#cbd5e1" }
                QGCTextField { id: camera2NameField; Layout.fillWidth: true }
                QGCTextField { id: camera2UrlField; Layout.fillWidth: true }

                QGCLabel { text: "3"; color: "#cbd5e1" }
                QGCTextField { id: camera3NameField; Layout.fillWidth: true }
                QGCTextField { id: camera3UrlField; Layout.fillWidth: true }

                QGCLabel { text: "4"; color: "#cbd5e1" }
                QGCTextField { id: camera4NameField; Layout.fillWidth: true }
                QGCTextField { id: camera4UrlField; Layout.fillWidth: true }
            }
        }
    }
}
