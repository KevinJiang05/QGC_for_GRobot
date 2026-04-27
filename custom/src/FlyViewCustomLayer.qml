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
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        width: Math.min(parent.width * 0.42, ScreenTools.defaultFontPixelWidth * 86)
        height: Math.min(parent.height * 0.58, width * 0.62)
    }
}
