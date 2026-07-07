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
    property real minimizedControlTopMargin: Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight)
    property real minimizedControlRightMargin: ScreenTools.defaultFontPixelWidth * 2
    property real minimizedControlGap: ScreenTools.defaultFontPixelHeight * 0.6
    property real statusPanelTopOffset: ScreenTools.defaultFontPixelHeight * 2
    property real restorePanelButtonWidth: Math.max(ScreenTools.defaultFontPixelWidth * 20, 190)
    property bool statusPanelManualOpen: false
    property bool statusPanelAutoCompact: width < ScreenTools.defaultFontPixelWidth * 145
                                          || height < ScreenTools.defaultFontPixelHeight * 46
    property bool statusPanelExpanded: (_root.videoMainMode || _root.panelMinimized)
                                       && !statusPanel.minimized
                                       && (!_root.statusPanelAutoCompact || _root.statusPanelManualOpen)
    property real rightPanelBottomMargin: Math.max(parentToolInsets.bottomEdgeRightInset,
                                                   ScreenTools.defaultFontPixelHeight * 3)
    property int sidePreviewMode: 0
    property bool attitudePreviewVisible: _root.statusPanelExpanded
                                          && !_root.panelMinimized
    property real attitudePreviewHeight: Math.max(ScreenTools.defaultFontPixelHeight * 8,
                                                  Math.min(height * 0.27,
                                                           ScreenTools.defaultFontPixelHeight * 15))
    property real videoPanelRightMargin: _root.videoMainMode
                                         ? (_root.statusPanelExpanded
                                            ? statusPanel.panelWidth + ScreenTools.defaultFontPixelWidth * 2
                                            : Math.max(parentToolInsets.rightEdgeBottomInset, ScreenTools.defaultFontPixelWidth * 2))
                                         : ScreenTools.defaultFontPixelWidth

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
    onStatusPanelAutoCompactChanged: {
        if (statusPanelAutoCompact) {
            statusPanelManualOpen = false
        }
    }

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
        anchors.rightMargin: _root.videoPanelRightMargin
        anchors.top: _root.videoMainMode ? parent.top : undefined
        anchors.topMargin: 0
        anchors.bottom: _root.videoMainMode ? parent.bottom : undefined
        anchors.bottomMargin: 0
        anchors.left: _root.videoMainMode ? parent.left : undefined
        anchors.leftMargin: _root.videoMainMode ? 0 : 0
        anchors.verticalCenter: _root.videoMainMode ? undefined : parent.verticalCenter
        width: _root.videoMainMode ? undefined : Math.min(parent.width * 0.42, ScreenTools.defaultFontPixelWidth * 86)
        height: _root.videoMainMode ? undefined : Math.min(parent.height * 0.58, width * 0.62)

        videoMainMode: _root.videoMainMode
        onToggleVideoMainMode: _root.videoMainMode = !_root.videoMainMode
        onToggleStatusPanel: {
            if (_root.statusPanelAutoCompact && !_root.statusPanelExpanded) {
                _root.statusPanelManualOpen = true
                statusPanel.minimized = false
            } else {
                statusPanel.toggleMinimized()
            }
        }
        onMinimizePanel: _root.minimizeDeepSharkPanel()
        onDeepSharkEvent: function(message) { _root.addDeepSharkEvent(message) }
    }

    DeepSharkStatusPanel {
        id: statusPanel
        visible: _root.statusPanelExpanded
        z: 20
        width: statusPanel.panelWidth
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: _root.attitudePreviewVisible ? sidePreview.top : parent.bottom
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        anchors.topMargin: _root.panelMinimized
                           ? _root.minimizedControlTopMargin + _root.compactStatusButtonHeight + _root.minimizedControlGap
                           : Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight) + _root.statusPanelTopOffset
        anchors.bottomMargin: _root.attitudePreviewVisible
                              ? _root.statusPanelGap
                              : _root.rightPanelBottomMargin

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

    Rectangle {
        id: sidePreview
        visible: _root.attitudePreviewVisible
        z: 20
        width: statusPanel.panelWidth
        height: _root.attitudePreviewHeight
        color: _root.sidePreviewMode === 0 ? "#07090c" : "#f7f9fb"
        border.color: "#aeb9c2"
        border.width: 1
        clip: true
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        anchors.bottomMargin: _root.rightPanelBottomMargin

        Rectangle {
            id: previewHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Math.max(ScreenTools.defaultFontPixelHeight * 2.1, 30)
            color: _root.sidePreviewMode === 0 ? "#e6141920" : "#efffffff"
            border.color: _root.sidePreviewMode === 0 ? "#384453" : "#aeb9c2"
            z: 3

            QGCLabel {
                anchors.left: parent.left
                anchors.leftMargin: ScreenTools.defaultFontPixelWidth
                anchors.verticalCenter: parent.verticalCenter
                text: _root.sidePreviewMode === 0 ? qsTr("RTSP 1") : qsTr("3D 姿态")
                color: _root.sidePreviewMode === 0 ? "#f2f5f8" : "#17212b"
                font.bold: true
            }

            ComboBox {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: Math.max(ScreenTools.defaultFontPixelWidth * 12, 92)
                model: [qsTr("RTSP 1"), qsTr("3D 姿态")]
                currentIndex: _root.sidePreviewMode
                onActivated: function(index) { _root.sidePreviewMode = index }
            }
        }

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: previewHeader.bottom
            anchors.bottom: parent.bottom
            clip: true

            Item {
                id: rtspPreviewArea
                anchors.fill: parent
                visible: _root.sidePreviewMode === 0

                ShaderEffectSource {
                    id: camera1Mirror
                    anchors.centerIn: parent
                    width: {
                        var sourceWidth = Math.max(1, fourVideoPanel.camera1VideoWidth)
                        var sourceHeight = Math.max(1, fourVideoPanel.camera1VideoHeight)
                        var sourceRatio = sourceWidth / sourceHeight
                        return sourceRatio > parent.width / Math.max(1, parent.height)
                               ? parent.width : parent.height * sourceRatio
                    }
                    height: {
                        var sourceWidth = Math.max(1, fourVideoPanel.camera1VideoWidth)
                        var sourceHeight = Math.max(1, fourVideoPanel.camera1VideoHeight)
                        var sourceRatio = sourceWidth / sourceHeight
                        return sourceRatio > parent.width / Math.max(1, parent.height)
                               ? parent.width / sourceRatio : parent.height
                    }
                    sourceItem: fourVideoPanel.camera1PreviewItem
                    live: true
                    recursive: true
                    visible: fourVideoPanel.camera1Decoding
                }

                QGCLabel {
                    anchors.centerIn: parent
                    visible: !fourVideoPanel.camera1Decoding
                    text: fourVideoPanel.camera1Status
                    color: "#9aa6b2"
                }
            }

            Attitude3DPanel {
                anchors.fill: parent
                visible: _root.sidePreviewMode === 1
                compactMode: true
                showCompactHeader: false
            }
        }
    }

    QGCButton {
        id: statusRestoreButton
        visible: (_root.videoMainMode || _root.panelMinimized) && statusPanel.minimized
                 || ((_root.videoMainMode || _root.panelMinimized) && _root.statusPanelAutoCompact && !_root.statusPanelExpanded)
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
                           ? _root.minimizedControlTopMargin
                             + _root.compactStatusButtonHeight
                             + _root.minimizedControlGap
                           : Math.max(parentToolInsets.topEdgeCenterInset, ScreenTools.defaultFontPixelHeight)
        onClicked: {
            if (_root.statusPanelAutoCompact && !_root.statusPanelExpanded) {
                _root.statusPanelManualOpen = true
                statusPanel.minimized = false
            } else {
                statusPanel.toggleMinimized()
            }
        }
    }

    Rectangle {
        id: restorePanelButton
        visible: _root.panelMinimized
        z: 30
        width: _root.restorePanelButtonWidth
        height: _root.compactStatusButtonHeight
        radius: 4
        color: "#334155"
        border.color: "#94a3b8"
        border.width: 1
        anchors.right: parent.right
        anchors.rightMargin: _root.minimizedControlRightMargin
        anchors.top: parent.top
        anchors.topMargin: _root.minimizedControlTopMargin

        QGCLabel {
            anchors.centerIn: parent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: ScreenTools.defaultFontPixelWidth
            anchors.rightMargin: ScreenTools.defaultFontPixelWidth
            text: qsTr("打开 DeepShark Panel")
            color: "#f8fafc"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        MouseArea {
            anchors.fill: parent
            onClicked: _root.restoreDeepSharkPanel()
        }
    }
}
