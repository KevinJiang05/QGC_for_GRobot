/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QGroundControl
import QGroundControl.Controls
import QGroundControl.ScreenTools
import QGroundControl.Palette

ColumnLayout {
    property var    instrumentValueData:            null
    property bool   settingsUnlocked:               false
    property alias  contentWidth:                   label.contentWidth
    readonly property bool _dataReady:              instrumentValueData !== null && instrumentValueData.factValueGrid !== null

    property var    _rgFontSizes:                   [ ScreenTools.defaultFontPointSize, ScreenTools.smallFontPointSize, ScreenTools.mediumFontPointSize, ScreenTools.largeFontPointSize ]
    property var    _rgFontSizeRatios:              [ 1, ScreenTools.smallFontPointRatio, ScreenTools.mediumFontPointRatio, ScreenTools.largeFontPointRatio ]
    property real   _doubleDescent:                 ScreenTools.defaultFontDescent * 2
    property real   _tightDefaultFontHeight:        ScreenTools.defaultFontPixelHeight - _doubleDescent
    property var    _rgFontSizeTightHeights:        [ _tightDefaultFontHeight * _rgFontSizeRatios[0] + 2, _tightDefaultFontHeight * _rgFontSizeRatios[1] + 2, _tightDefaultFontHeight * _rgFontSizeRatios[2] + 2, _tightDefaultFontHeight * _rgFontSizeRatios[3] + 2 ]
    property real   _tightHeight:                   _dataReady ? _rgFontSizeTightHeights[instrumentValueData.factValueGrid.fontSize] : ScreenTools.defaultFontPixelHeight
    property real   _fontSize:                      Math.max(1, _dataReady ? _rgFontSizes[instrumentValueData.factValueGrid.fontSize] : ScreenTools.defaultFontPointSize)
    property real   _horizontalLabelSpacing:        ScreenTools.defaultFontPixelWidth
    property real   _width:                         0
    property real   _height:                        0

    visible: _dataReady

    QGCLabel {
        id:                 label
        Layout.alignment:   Qt.AlignVCenter
        font.pointSize:     _fontSize
        color:              _dataReady && instrumentValueData.isValidColor(instrumentValueData.currentColor) ? instrumentValueData.currentColor : qgcPal.text
        text:               valueText()

        function valueText() {
            if (_dataReady && instrumentValueData.fact) {
                return instrumentValueData.fact.enumOrValueString + (instrumentValueData.showUnits ? " " + instrumentValueData.fact.units : "")
            } else {
                return qsTr("--.--")
            }
        }
    }
}
