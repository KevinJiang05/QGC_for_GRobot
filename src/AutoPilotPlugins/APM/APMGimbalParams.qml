import QtQuick

import QGroundControl

/// Manages access to ArduPilot mount parameters
Item {
    /// The MNT#_ parameters this object manages
    property int instance: 1

    property var controller: FactPanelController {}

    /// Number of MNT#_ instances available, 0 indicates no gimbal support
    readonly property int instanceCount: _instanceCount

    /// True if gimbal parameters are available, False indicates MNT#_TYPE disabled, or reboot required to get params
    property bool paramsAvailable: false

    readonly property bool legacyParameters: instance === 1 && !controller.parameterExists(-1, "MNT1_TYPE") &&
                                            (controller.parameterExists(-1, "MNT_TYPE") ||
                                             controller.parameterExists(-1, "MNT_RC_IN_TILT") ||
                                             controller.parameterExists(-1, "MNT_DEFLT_MODE"))

    property Fact typeFact: controller.getParameterFact(-1, _prefixTemplate.replace("#", instance) + "TYPE", false)
    property Fact defaultModeFact: controller.getParameterFact(-1, _prefixTemplate.replace("#", instance) + "DEFLT_MODE", false)

    property Fact optionsFact: null
    property Fact neutralXFact: null
    property Fact neutralYFact: null
    property Fact neutralZFact: null
    property Fact retractXFact: null
    property Fact retractYFact: null
    property Fact retractZFact: null
    property Fact pitchMinFact: null
    property Fact pitchMaxFact: null
    property Fact rollMinFact: null
    property Fact rollMaxFact: null
    property Fact yawMinFact: null
    property Fact yawMaxFact: null
    property Fact rcRateFact: null
    property Fact pitchLeadFact: null
    property Fact rollLeadFact: null
    property Fact pitchInputFact: null
    property Fact rollInputFact: null
    property Fact yawInputFact: null
    property Fact pitchStabilizeFact: null
    property Fact rollStabilizeFact: null
    property Fact yawStabilizeFact: null
    property Fact joystickSpeedFact: null

    property int _instanceCount: _instancedParamCount("MNT#_TYPE") || (legacyParameters ? 1 : 0)
    property string _prefixTemplate: legacyParameters ? "MNT_" : "MNT#_"

    function _optionalFact(suffix) {
        return controller.getParameterFact(-1, _prefixTemplate.replace("#", instance) + suffix, false)
    }

    function _instancedParamCount(paramNameTemplate) {
        let instanceIndex = 1
        while (controller.parameterExists(-1, paramNameTemplate.replace("#", instanceIndex))) {
            instanceIndex++
        }
        return instanceIndex - 1
    }

    Component.onCompleted: {
        if (defaultModeFact === null && !legacyParameters) {
            paramsAvailable = false
        } else {
            optionsFact = _optionalFact("OPTIONS")
            neutralXFact = _optionalFact("NEUTRAL_X")
            neutralYFact = _optionalFact("NEUTRAL_Y")
            neutralZFact = _optionalFact("NEUTRAL_Z")
            retractXFact = _optionalFact("RETRACT_X")
            retractYFact = _optionalFact("RETRACT_Y")
            retractZFact = _optionalFact("RETRACT_Z")
            // Keep the original Facts and their metadata/unit conversion for old angle parameters.
            pitchMinFact = _optionalFact(legacyParameters ? "ANGMIN_TIL" : "PITCH_MIN")
            pitchMaxFact = _optionalFact(legacyParameters ? "ANGMAX_TIL" : "PITCH_MAX")
            rollMinFact = _optionalFact(legacyParameters ? "ANGMIN_ROL" : "ROLL_MIN")
            rollMaxFact = _optionalFact(legacyParameters ? "ANGMAX_ROL" : "ROLL_MAX")
            yawMinFact = _optionalFact(legacyParameters ? "ANGMIN_PAN" : "YAW_MIN")
            yawMaxFact = _optionalFact(legacyParameters ? "ANGMAX_PAN" : "YAW_MAX")
            rcRateFact = legacyParameters ? null : _optionalFact("RC_RATE")
            pitchLeadFact = _optionalFact("LEAD_PTCH")
            rollLeadFact = _optionalFact("LEAD_RLL")
            if (legacyParameters) {
                pitchInputFact = _optionalFact("RC_IN_TILT")
                rollInputFact = _optionalFact("RC_IN_ROLL")
                yawInputFact = _optionalFact("RC_IN_PAN")
                pitchStabilizeFact = _optionalFact("STAB_TILT")
                rollStabilizeFact = _optionalFact("STAB_ROLL")
                yawStabilizeFact = _optionalFact("STAB_PAN")
                joystickSpeedFact = _optionalFact("JSTICK_SPD")
            }
            paramsAvailable = true
        }
    }
}
