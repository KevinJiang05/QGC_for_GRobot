import QGroundControl
/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick

import DeepShark 1.0
import QGroundControl.Controls

Item {
    id: root

    property string sourceId: ""
    property real videoWidth: 0
    property real videoHeight: 0
    property var detectionModel: []
    property bool _hasVideoSize: videoWidth > 0 && videoHeight > 0
    property real _videoAspect: _hasVideoSize ? videoWidth / videoHeight : 1
    property real _rootAspect: height > 0 ? width / height : 1
    property real _contentWidth: !_hasVideoSize ? width : (_rootAspect > _videoAspect ? height * _videoAspect : width)
    property real _contentHeight: !_hasVideoSize ? height : (_rootAspect > _videoAspect ? height : width / _videoAspect)

    clip: true

    function refreshDetections() {
        detectionModel = AIDetectionReceiver.detectionsForSource(sourceId)
    }

    Component.onCompleted: refreshDetections()
    onSourceIdChanged: refreshDetections()

    Connections {
        target: AIDetectionReceiver
        function onDetectionsChanged() {
            root.refreshDetections()
        }
    }

    Item {
        id: contentArea

        x: Math.max(0, (root.width - root._contentWidth) / 2)
        y: Math.max(0, (root.height - root._contentHeight) / 2)
        width: Math.max(0, root._contentWidth)
        height: Math.max(0, root._contentHeight)
        clip: true

        Repeater {
            model: root.detectionModel

            Rectangle {
                required property var modelData

                x:              modelData.x * contentArea.width
                y:              modelData.y * contentArea.height
                width:          modelData.w * contentArea.width
                height:         modelData.h * contentArea.height
                color:          "transparent"
                border.color:   "#00e5ff"
                border.width:   Math.max(2, ScreenTools.defaultFontPixelWidth / 5)

                Rectangle {
                    id:                 detectionLabelBackground
                    anchors.left:       parent.left
                    anchors.bottom:     parent.top
                    width:              detectionLabel.contentWidth + ScreenTools.defaultFontPixelWidth
                    height:             detectionLabel.contentHeight + ScreenTools.defaultFontPixelHeight / 3
                    color:              "#00e5ff"
                    opacity:            0.85
                    visible:            parent.y > height
                }

                QGCLabel {
                    id:                 detectionLabel
                    anchors.centerIn:   detectionLabelBackground
                    text:               modelData.label + " " + Math.round(modelData.confidence * 100) + "%"
                    color:              "black"
                    font.bold:          true
                    font.pointSize:     ScreenTools.smallFontPointSize
                    visible:            detectionLabelBackground.visible
                }
            }
        }
    }
}
