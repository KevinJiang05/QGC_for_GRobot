/****************************************************************************
 *
 * DeepShark Fly View custom layer.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Controls

import "qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark"

import QGroundControl
import QGroundControl.Controls
import QGroundControl.Palette
import QGroundControl.ScreenTools

Item {
    id: _root

    property var parentToolInsets
    property var totalToolInsets: _toolInsets
    property var mapControl
    property bool videoMainMode: true
    property bool panelMinimized: false
    property bool mapHidden: videoMainMode && !panelMinimized
    property var recentEvents: []
    property real statusPanelGap: ScreenTools.defaultFontPixelWidth
    property real compactStatusButtonWidth: Math.max(ScreenTools.defaultFontPixelWidth * 5, 42)
    property real compactStatusButtonHeight: Math.max(ScreenTools.defaultFontPixelHeight * 2.2, 32)

    function addDeepSharkEvent(message) {
        var timestamp = Qt.formatTime(new Date(), "hh:mm:ss")
        var updatedEvents = recentEvents.slice(0)
        updatedEvents.unshift(timestamp + "  " + message)
        if (updatedEvents.length > 50) {
            updatedEvents.length = 50
        }
        recentEvents = updatedEvents
    }

    function syncMapControlVisibility() {
        if (typeof mapControl === "undefined" || mapControl === null) {
            return
        }

        var hideMap = videoMainMode && !panelMinimized
        mapHidden = hideMap
        mapControl.visible = !hideMap
        mapControl.enabled = !hideMap
        mapControl.opacity = hideMap ? 0 : 1
    }

    function minimizeDeepSharkPanel() {
        addDeepSharkEvent("DeepShark panel minimized")
        panelMinimized = true
        videoMainMode = false
        syncMapControlVisibility()
    }

    function restoreDeepSharkPanel() {
        addDeepSharkEvent("DeepShark panel restored")
        panelMinimized = false
        videoMainMode = true
        syncMapControlVisibility()
    }

    onVideoMainModeChanged: {
        addDeepSharkEvent(videoMainMode ? "Map hidden" : "Map visible")
        syncMapControlVisibility()
    }
    onPanelMinimizedChanged: syncMapControlVisibility()
    onMapControlChanged: Qt.callLater(syncMapControlVisibility)

    Component.onCompleted: Qt.callLater(syncMapControlVisibility)

    QGCPalette {
        id: qgcPal
        colorGroupEnabled: enabled
    }

    QGCToolInsets {
        id: _toolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }

    FourVideoPanel {
        id: fourVideoPanel
        visible: !_root.panelMinimized
        anchors.right: parent.right
        anchors.rightMargin: _root.videoMainMode
                             ? Math.max(parentToolInsets.rightEdgeBottomInset, ScreenTools.defaultFontPixelWidth * 2)
                             : ScreenTools.defaultFontPixelWidth
        anchors.top: _root.videoMainMode ? parent.top : undefined
        anchors.topMargin: _root.videoMainMode ? 0 : 0
        anchors.bottom: _root.videoMainMode ? parent.bottom : undefined
        anchors.bottomMargin: 0
        anchors.left: _root.videoMainMode ? parent.left : undefined
        anchors.leftMargin: _root.videoMainMode ? 0 : 0
        anchors.verticalCenter: _root.videoMainMode ? undefined : parent.verticalCenter
        width: _root.videoMainMode ? undefined : Math.min(parent.width * 0.42, ScreenTools.defaultFontPixelWidth * 86)
        height: _root.videoMainMode ? undefined : Math.min(parent.height * 0.58, width * 0.62)

        videoMainMode: _root.videoMainMode
        onToggleVideoMainMode: _root.videoMainMode = !_root.videoMainMode
        onMinimizePanel: _root.minimizeDeepSharkPanel()
        onDeepSharkEvent: function(message) { _root.addDeepSharkEvent(message) }
    }

    DeepSharkStatusPanel {
        id: statusPanel
        visible: (_root.videoMainMode || _root.panelMinimized) && !statusPanel.minimized
        z: 20
        width: statusPanel.panelWidth
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        anchors.topMargin: Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight)
        anchors.bottomMargin: Math.max(parentToolInsets.bottomEdgeRightInset, ScreenTools.defaultFontPixelHeight * 3)

        layoutMode: _root.panelMinimized ? "minimized" : fourVideoPanel.layoutMode
        mainIndex: fourVideoPanel.mainIndex
        mainName: fourVideoPanel.mainViewName
        mapHidden: _root.mapHidden
        deepSharkPanelMinimized: _root.panelMinimized
        vehicleStatus: "Unknown"
        videoRows: fourVideoPanel.videoRows
        rtspRows: fourVideoPanel.videoRows
        recentEvents: _root.recentEvents
        onStatusPanelEvent: function(message) { _root.addDeepSharkEvent(message) }
        onReconnectVideo: function(index) { fourVideoPanel.reconnectVideo(index) }
        onReconnectAllVideos: fourVideoPanel.reconnectAllVideos()
    }

    QGCButton {
        id: statusRestoreButton
        visible: (_root.videoMainMode || _root.panelMinimized) && statusPanel.minimized
        z: 20
        width: _root.compactStatusButtonWidth
        height: _root.compactStatusButtonHeight
        text: qsTr("状态")
        backgroundColor: "#334155"
        textColor: "#f8fafc"
        showBorder: true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        anchors.topMargin: _root.panelMinimized
                           ? Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight)
                             + _root.compactStatusButtonHeight
                             + ScreenTools.defaultFontPixelHeight
                           : Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight)
        onClicked: statusPanel.toggleMinimized()
    }

    QGCButton {
        id: restorePanelButton
        visible: _root.panelMinimized
        text: qsTr("打开 DeepShark Panel")
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth * 2
        anchors.topMargin: Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight)
        onClicked: _root.restoreDeepSharkPanel()
    }
}
