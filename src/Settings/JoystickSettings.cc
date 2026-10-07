/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "JoystickSettings.h"

#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <algorithm>

JoystickSettings::JoystickSettings(const QString &joystickName, int axisCount, int buttonCount, QObject* parent)
    : SettingsGroup("Joystick", QString("JoystickSettingsV2/%1").arg(joystickName), parent)
    , _joystickName(joystickName)
    , _axisCount(axisCount)
    , _buttonCount(buttonCount)
{
    // Import only from this application's settings store. The Debug candidate has its own
    // identity, so this never opens or changes the installed application's calibration.
    QSettings settings;
    settings.beginGroup(settingsGroup());
    const bool hasNewSettings = !settings.childKeys().isEmpty() || !settings.childGroups().isEmpty();
    settings.endGroup();
    if (hasNewSettings) {
        return;
    }

    const QString legacyGroup = QStringLiteral("Joysticks/%1/").arg(joystickName);
    settings.beginGroup(legacyGroup);
    const bool hasLegacySettings = !settings.childKeys().isEmpty();
    settings.endGroup();
    if (!hasLegacySettings) {
        return;
    }

    const QString newGroup = settingsGroup() + '/';
    const auto copySetting = [&](const char* oldKey, const char* newKey, const QVariant& fallback) {
        settings.setValue(newGroup + newKey, settings.value(legacyGroup + oldKey, fallback));
    };
    copySetting("Circle_Correction", "circleCorrection", false);
    copySetting("Deadband", "useDeadband", false);
    copySetting("NegativeThrust", "negativeThrust", false);
    copySetting("Accumulator", "throttleSmoothing", false);
    copySetting("AxisFrequency", "axisFrequencyHz", 25.0);
    copySetting("ButtonFrequency", "buttonFrequencyHz", 5.0);
    settings.setValue(newGroup + "throttleModeCenterZero",
                      settings.value(legacyGroup + "ThrottleMode", 1).toInt() == 0);
    settings.setValue(newGroup + "exponentialPct",
                      std::clamp(-100.0 * settings.value(legacyGroup + "Exponential", 0.0).toDouble(), 0.0, 50.0));
    // ArduSub used a shared transmitter mode, defaulting to mode 3; V2 stores it per joystick.
    settings.setValue(newGroup + "transmitterMode",
                      std::clamp(settings.value("Joysticks/TXMode_Submarine", 3).toInt(), 1, 4));

    static constexpr const char* functionKeys[] = {"RollAxis", "PitchAxis", "YawAxis", "ThrottleAxis"};
    QSet<int> assignedAxes;
    const QString calibrated = settings.value(legacyGroup + "Calibrated4", false).toString().toLower();
    bool validCalibration = calibrated == "true" || calibrated == "1";
    for (int function = 0; function < 4; ++function) {
        bool axisOk = false;
        const int axis = settings.value(legacyGroup + functionKeys[function], -1).toInt(&axisOk);
        if (!axisOk || axis < 0 || axis >= axisCount || assignedAxes.contains(axis)) {
            validCalibration = false;
            continue;
        }
        assignedAxes.insert(axis);
        const QString oldAxis = legacyGroup + QStringLiteral("Axis%1").arg(axis);
        const QString newAxis = newGroup + QStringLiteral("JoystickAxisSettingsArray/%1/").arg(axis);
        bool minimumOk = false;
        bool maximumOk = false;
        bool centerOk = false;
        bool deadbandOk = false;
        const int minimum = settings.value(oldAxis + "Min").toInt(&minimumOk);
        const int maximum = settings.value(oldAxis + "Max").toInt(&maximumOk);
        const int center = settings.value(oldAxis + "Trim").toInt(&centerOk);
        const int deadband = settings.value(oldAxis + "Deadbnd").toInt(&deadbandOk);
        const QString reversed = settings.value(oldAxis + "Rev").toString().toLower();
        const bool reversedOk = reversed == "true" || reversed == "false" || reversed == "1" || reversed == "0";
        // Old calibration saves all five fields. Defaults cannot prove that the device was calibrated.
        validCalibration &= minimumOk && maximumOk && centerOk && deadbandOk && reversedOk && minimum >= -32768 &&
                            maximum <= 32767 && minimum < maximum && minimum <= center && center <= maximum &&
                            deadband >= 0 && deadband < maximum - minimum;
        settings.setValue(newAxis + "function", function);  // Both versions persist mappings in TX mode 2.
        settings.setValue(newAxis + "min", minimum);
        settings.setValue(newAxis + "max", maximum);
        settings.setValue(newAxis + "center", center);
        settings.setValue(newAxis + "deadband", deadband);
        settings.setValue(newAxis + "reversed", settings.value(oldAxis + "Rev", false));
    }
    settings.setValue(newGroup + "calibrated", validCalibration);

    for (int button = 0; button < buttonCount; ++button) {
        const QString action =
            settings.value(legacyGroup + QStringLiteral("ButtonActionName%1").arg(button)).toString();
        if (action.isEmpty() || action == "No Action") {
            continue;
        }
        const QString newButton = newGroup + QStringLiteral("JoystickButtonActionSettingsArray/%1/").arg(button);
        settings.setValue(newButton + "actionName", action);
        settings.setValue(newButton + "repeat",
                          settings.value(legacyGroup + QStringLiteral("ButtonActionRepeat%1").arg(button), false));
    }
    // Leave all legacy keys intact, including retired gimbal axis assignments. Those axes
    // must not be silently converted to V2's different MANUAL_CONTROL/RC override functions.
}

DECLARE_SETTINGSFACT(JoystickSettings, calibrated)
DECLARE_SETTINGSFACT(JoystickSettings, circleCorrection)
DECLARE_SETTINGSFACT(JoystickSettings, useDeadband)
DECLARE_SETTINGSFACT(JoystickSettings, negativeThrust)
DECLARE_SETTINGSFACT(JoystickSettings, throttleSmoothing)
DECLARE_SETTINGSFACT(JoystickSettings, axisFrequencyHz)
DECLARE_SETTINGSFACT(JoystickSettings, buttonFrequencyHz)
DECLARE_SETTINGSFACT(JoystickSettings, throttleModeCenterZero)
DECLARE_SETTINGSFACT(JoystickSettings, transmitterMode)
DECLARE_SETTINGSFACT(JoystickSettings, exponentialPct)
DECLARE_SETTINGSFACT(JoystickSettings, enableManualControlPitchExtension)
DECLARE_SETTINGSFACT(JoystickSettings, enableManualControlRollExtension)
DECLARE_SETTINGSFACT(JoystickSettings, additionalAxesFunction)
DECLARE_SETTINGSFACT(JoystickSettings, enableAdditionalAxis1)
DECLARE_SETTINGSFACT(JoystickSettings, enableAdditionalAxis2)
DECLARE_SETTINGSFACT(JoystickSettings, enableAdditionalAxis3)
DECLARE_SETTINGSFACT(JoystickSettings, enableAdditionalAxis4)
DECLARE_SETTINGSFACT(JoystickSettings, enableAdditionalAxis5)
DECLARE_SETTINGSFACT(JoystickSettings, enableAdditionalAxis6)
