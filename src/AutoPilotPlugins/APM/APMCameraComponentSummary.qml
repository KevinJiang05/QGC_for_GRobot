import QtQuick
import QtQuick.Controls

import QGroundControl.FactSystem
import QGroundControl.FactControls
import QGroundControl.Controls
import QGroundControl.Palette

Item {
    anchors.fill:   parent

    FactPanelController { id: controller; }

    property bool _legacyMountParamsAvailable:  controller.parameterExists(-1, "MNT_RC_IN_TILT")
    property bool _mount1ParamsAvailable:       controller.parameterExists(-1, "MNT1_TYPE")
    property Fact _mountRCInTilt:                _legacyMountParamsAvailable ? controller.getParameterFact(-1, "MNT_RC_IN_TILT", false) : null
    property Fact _mountRCInRoll:                _legacyMountParamsAvailable ? controller.getParameterFact(-1, "MNT_RC_IN_ROLL", false) : null
    property Fact _mountRCInPan:                 _legacyMountParamsAvailable ? controller.getParameterFact(-1, "MNT_RC_IN_PAN", false) : null

    // MNT_TYPE parameter is not in older firmware versions
    property bool   _mountTypeExists: controller.parameterExists(-1, "MNT_TYPE") || _mount1ParamsAvailable
    property string _mountTypeValue: _mountTypeExists
                                            ? controller.getParameterFact(-1, _mount1ParamsAvailable ? "MNT1_TYPE" : "MNT_TYPE", false).enumStringValue
                                            : ""

    Column {
        anchors.fill:       parent

        VehicleSummaryRow {
            visible:    _mountTypeExists
            labelText:  qsTr("Gimbal type")
            valueText:  _mountTypeValue
        }

        VehicleSummaryRow {
            visible:    _legacyMountParamsAvailable
            labelText:  qsTr("Tilt input channel")
            valueText:  _mountRCInTilt ? _mountRCInTilt.enumStringValue : ""
        }

        VehicleSummaryRow {
            visible:    _legacyMountParamsAvailable
            labelText:  qsTr("Pan input channel")
            valueText:  _mountRCInPan ? _mountRCInPan.enumStringValue : ""
        }

        VehicleSummaryRow {
            visible:    _legacyMountParamsAvailable
            labelText:  qsTr("Roll input channel")
            valueText:  _mountRCInRoll ? _mountRCInRoll.enumStringValue : ""
        }

        VehicleSummaryRow {
            visible:    !_legacyMountParamsAvailable
            labelText:  qsTr("Mount parameters")
            valueText:  _mount1ParamsAvailable ? qsTr("MNT1/MNT2") : qsTr("Not available")
        }
    }
}
