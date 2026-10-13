#include "JoystickConfigController.h"

#include <QtCore/QSettings>

#include "Fact.h"
#include "Joystick.h"
#include "ParameterManager.h"
#include "AppMessages.h"
#include "JoystickManagerSettings.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "Vehicle.h"

QGC_LOGGING_CATEGORY(JoystickConfigControllerLog, "AutoPilotPlugins.JoystickConfigController")
QGC_LOGGING_CATEGORY(JoystickConfigControllerVerboseLog, "AutoPilotPlugins.JoystickConfigController:verbose")

JoystickConfigController::JoystickConfigController(QObject *parent)
    : RemoteControlCalibrationController(parent)
{
    // Channel values are based on -32768:32767 range from SDL

    _calCenterPoint =       0;
    _calDefaultMinValue =   -32768;     ///< Default value for Min if not set
    _calDefaultMaxValue =   32767;      ///< Default value for Max if not set
    _calRoughCenterDelta =  700;        ///< Delta around center point which is considered to be roughly centered (Note: PS5 controller has very noisy center, hence 700)
    _calMoveDelta =         32768/2;    ///< Amount of delta past center which is considered stick movement
    _calSettleDelta =       1000;       ///< Amount of delta which is considered no stick movement (increased for noisy joysticks at extreme positions)
    _stickDetectSettleMSecs = 300;      ///< Reduced settle time for joysticks (faster response than RC transmitters)

    int valueRange = _calDefaultMaxValue - _calDefaultMinValue;
    _calValidMinValue = _calDefaultMinValue + (valueRange * 0.3f);
    _calValidMaxValue = _calDefaultMaxValue - (valueRange * 0.3f);

    connect(this, &RemoteControlCalibrationController::calibratingChanged, this,
            &JoystickConfigController::_updateConfigurationVehicleControl);
    connect(this, &RemoteControlCalibrationController::transmitterModeChanged, this,
            &JoystickConfigController::_applyTransmitterMode);
    if (_vehicle) {
        connect(_vehicle, &Vehicle::setupSafetyRestrictionsDisabledChanged, this,
                &JoystickConfigController::_updateConfigurationVehicleControl);
    }
}

JoystickConfigController::~JoystickConfigController()
{
    if (_joystick) {
        _joystick->_configurationAllowsVehicleControl.store(false);
        if (_joystick->_pollingFlags.testFlag(Joystick::PollingForConfiguration)) {
            _joystick->_stopPollingForConfiguration();
        }
    }
}

Joystick* JoystickConfigController::joystick() const
{
    return _joystick.data();
}

void JoystickConfigController::start(void)
{
    if (!_joystick) {
        qCWarning(JoystickConfigControllerLog) << "No joystick set!";
        return;
    }

    qCDebug(JoystickConfigControllerLog) << "Starting joystick configuration for joystick:" << _joystick->name();
    RemoteControlCalibrationController::start();
    _updateConfigurationVehicleControl();
    _joystick->_startPollingForConfiguration();
}

void JoystickConfigController::_applyTransmitterMode()
{
    // Upstream stores the mode only when a calibration is completed, so a mode that was merely selected was
    // gone as soon as the page was left. A change of mode needs no new calibration: the axis assignments
    // are stored as for mode 2 and remapped when they are loaded. The selection is therefore stored at
    // once, and the assignments in use are remapped the way a restart would remap them.
    if (!_joystick || calibrating()) {
        return;
    }
    Fact *const storedMode = _joystick->settings()->transmitterMode();
    const int previousMode = storedMode->rawValue().toInt();
    const int newMode = transmitterMode();
    if ((previousMode == newMode) || (previousMode < 1) || (previousMode > 4)) {
        return;
    }
    if (_vehicle && _vehicle->armed()) {
        // The sticks would change their meaning under the operator's hands.
        setTransmitterMode(previousMode);
        QGC::showAppMessage(tr("请先上锁，再切换摇杆模式。"));
        return;
    }

    // With the setup safety restrictions off this page controls the vehicle on purpose, and the operator who
    // switched them off decides about the mode as well. Only the armed check above remains.
    const bool restrictionsOff = _vehicle && _vehicle->setupSafetyRestrictionsDisabled();
    bool controlsVehicle = false;
    {
        // A scan reads the assignments while it holds this lock.
        const QMutexLocker pollingState(&_joystick->_pollingStateMutex);
        // The vehicle reports that it is armed some time after it was armed, so the check above does not
        // cover a joystick that is enabled for a vehicle. This page suspends its control while it is open,
        // but that is not enough: the page closes by itself when the armed state arrives, and control
        // would come back with the new assignments without anyone having asked for it. Control has to be
        // turned off first.
        controlsVehicle = !restrictionsOff && _joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle) &&
                          _joystick->settings()->calibrated()->rawValue().toBool();
        if (!controlsVehicle) {
            // Assignments that an invalidated calibration left behind are remapped as well, so that the
            // monitor never shows them under the name of another mode. They stay without effect.
            _joystick->_remapFunctionsInFunctionMapToNewTransmittedMode(previousMode, 2);
            _joystick->_remapFunctionsInFunctionMapToNewTransmittedMode(2, newMode);
        }
    }
    if (controlsVehicle) {
        setTransmitterMode(previousMode);
        QGC::showAppMessage(tr("手柄控制处于启用状态。请先取消“启用手柄控制”，再切换摇杆模式，然后重新启用。"));
        return;
    }
    // The assignments belong to the joystick, the permission to control belongs to each vehicle. A vehicle
    // that still has it would be controlled with the new assignments the moment it becomes the active one,
    // for instance when this one disconnects. So the permission ends for all of them, and each vehicle is
    // controlled again once the operator enables the joystick for it.
    Fact *const enabledVehicles = SettingsManager::instance()->joystickManagerSettings()->joystickEnabledVehiclesIds();
    // With the restrictions off the joystick may be enabled for this very vehicle, and it stays so.
    const bool otherVehiclesWereEnabled = !restrictionsOff && !enabledVehicles->rawValue().toString().isEmpty();
    if (otherVehiclesWereEnabled) {
        enabledVehicles->setRawValue(QString());
    }
    storedMode->setRawValue(newMode);
    _readStoredCalibrationValues();
    if (otherVehiclesWereEnabled) {
        QGC::showAppMessage(tr("摇杆模式已切换。其他机器人上的手柄控制也已停用，需要在各自连接后重新启用。"));
    }
}

void JoystickConfigController::_updateConfigurationVehicleControl()
{
    if (_joystick) {
        _joystick->_configurationAllowsVehicleControl.store(_vehicle && _vehicle->setupSafetyRestrictionsDisabled() &&
                                                            !calibrating());
    }
}

void JoystickConfigController::_setJoystick(Joystick* joystick)
{
    if (!joystick) {
        qCWarning(JoystickConfigControllerLog) << "Cannot set joystick: null joystick";
        return;
    }
    if (_joystick) {
        qCWarning(JoystickConfigControllerLog) << "Joystick should only be set a single time";
        return;
    }

    _joystick = joystick;
    _updateConfigurationVehicleControl();
    _readStoredCalibrationValues();
    connect(_joystick, &Joystick::rawChannelValuesChanged, this, &JoystickConfigController::_rawChannelValuesChanged);

    // Connect to settings changes to emit extension enabled signals
    connect(_joystick->settings()->enableManualControlPitchExtension(), &Fact::rawValueChanged, this, [this]() {
        emit pitchExtensionEnabledChanged(pitchExtensionEnabled());
    });
    connect(_joystick->settings()->enableManualControlRollExtension(), &Fact::rawValueChanged, this, [this]() {
        emit rollExtensionEnabledChanged(rollExtensionEnabled());
    });
    connect(_joystick->settings()->enableAdditionalAxis1(), &Fact::rawValueChanged, this, [this]() {
        emit additionalAxis1EnabledChanged(additionalAxis1Enabled());
    });
    connect(_joystick->settings()->enableAdditionalAxis2(), &Fact::rawValueChanged, this, [this]() {
        emit additionalAxis2EnabledChanged(additionalAxis2Enabled());
    });
    connect(_joystick->settings()->enableAdditionalAxis3(), &Fact::rawValueChanged, this, [this]() {
        emit additionalAxis3EnabledChanged(additionalAxis3Enabled());
    });
    connect(_joystick->settings()->enableAdditionalAxis4(), &Fact::rawValueChanged, this, [this]() {
        emit additionalAxis4EnabledChanged(additionalAxis4Enabled());
    });
    connect(_joystick->settings()->enableAdditionalAxis5(), &Fact::rawValueChanged, this, [this]() {
        emit additionalAxis5EnabledChanged(additionalAxis5Enabled());
    });
    connect(_joystick->settings()->enableAdditionalAxis6(), &Fact::rawValueChanged, this, [this]() {
        emit additionalAxis6EnabledChanged(additionalAxis6Enabled());
    });

    emit joystickChanged(_joystick);
}

void JoystickConfigController::_saveStoredCalibrationValues()
{
    if (!_joystick) {
        qCWarning(JoystickConfigControllerLog) << "Internal Error: No joystick to save calibration for";
        return;
    }

    qCDebug(JoystickConfigControllerLog) << _joystick->name();

    _validateAndAdjustCalibrationValues();

    _joystick->settings()->calibrated()->setRawValue(true);
    _joystick->settings()->transmitterMode()->setRawValue(transmitterMode());

    for (int chan = 0; chan < _joystick->axisCount(); chan++) {
        ChannelInfo *const channelInfo = &_rgChannelInfo[chan];
        Joystick::AxisCalibration_t joystickAxisInfo;

        qCDebug(JoystickConfigControllerLog)
            << "chan:" <<       chan
            << "function:" <<   _stickFunctionToString(channelInfo->stickFunction)
            << "min:" <<        channelInfo->channelMin
            << "max:" <<        channelInfo->channelMax
            << "trim:" <<       channelInfo->channelTrim
            << "reversed:" <<   channelInfo->channelReversed
            << "deadband:" <<   channelInfo->deadband;

        joystickAxisInfo.center = channelInfo->channelTrim;
        joystickAxisInfo.min = channelInfo->channelMin;
        joystickAxisInfo.max = channelInfo->channelMax;
        joystickAxisInfo.reversed = channelInfo->channelReversed;
        joystickAxisInfo.deadband = channelInfo->deadband;
        _joystick->setAxisCalibration(chan, joystickAxisInfo);

        _joystick->setFunctionForChannel(channelInfo->stickFunction, chan);
    }

    _joystick->_saveFromCalibrationDataIntoSettings();

    // Read back since validation may have changed values
    _readStoredCalibrationValues();
}

void JoystickConfigController::_readStoredCalibrationValues()
{
    if (!_joystick) {
        qCWarning(JoystickConfigControllerLog) << "Internal Error: No joystick to read calibration for";
        return;
    }

    setTransmitterMode(_joystick->settings()->transmitterMode()->rawValue().toInt());

    // Initialize all function mappings to not set
    for (int i = 0; i < _chanMax; i++) {
        ChannelInfo *const info = &_rgChannelInfo[i];
        info->stickFunction = stickFunctionMax;
    }
    for (size_t i = 0; i < stickFunctionMax; i++) {
        _rgFunctionChannelMapping[i] = _chanMax;
    }

    for (int i = 0; i < _joystick->axisCount(); ++i) {
        ChannelInfo *const info = &_rgChannelInfo[i];
        Joystick::AxisCalibration_t calibration = _joystick->getAxisCalibration(i);

        info->channelTrim  = calibration.center;
        info->channelMin   = calibration.min;
        info->channelMax   = calibration.max;
        info->channelReversed  = calibration.reversed;
        info->deadband  = calibration.deadband;
    }

    for (int stickFunctionInt = 0; stickFunctionInt < Joystick::maxAxisFunction; stickFunctionInt++) {
        auto stickFunction = static_cast<StickFunction>(stickFunctionInt);
        int functionChannel = _joystick->getChannelForFunction(stickFunction);
        if (functionChannel >= 0 && functionChannel < _chanMax) {
            _rgFunctionChannelMapping[stickFunction] = functionChannel;
            _rgChannelInfo[functionChannel].stickFunction = stickFunction;
        }
    }

    _signalAllAttitudeValueChanges();
}


bool JoystickConfigController::_stickFunctionEnabled(StickFunction stickFunction)
{
    if (RemoteControlCalibrationController::_stickFunctionEnabled(stickFunction)) {
        return true;
    }

    // QML can query extensions before applying the required joystick binding.
    if (!_joystick) {
        return false;
    }

    switch (stickFunction) {
    case stickFunctionRollExtension:
        return _joystick->settings()->enableManualControlRollExtension()->rawValue().toBool();
    case stickFunctionPitchExtension:
        return _joystick->settings()->enableManualControlPitchExtension()->rawValue().toBool();
    case stickFunctionAdditionalAxis1:
        return _joystick->settings()->enableAdditionalAxis1()->rawValue().toBool();
    case stickFunctionAdditionalAxis2:
        return _joystick->settings()->enableAdditionalAxis2()->rawValue().toBool();
    case stickFunctionAdditionalAxis3:
        return _joystick->settings()->enableAdditionalAxis3()->rawValue().toBool();
    case stickFunctionAdditionalAxis4:
        return _joystick->settings()->enableAdditionalAxis4()->rawValue().toBool();
    case stickFunctionAdditionalAxis5:
        return _joystick->settings()->enableAdditionalAxis5()->rawValue().toBool();
    case stickFunctionAdditionalAxis6:
        return _joystick->settings()->enableAdditionalAxis6()->rawValue().toBool();
    case stickFunctionRoll:
    case stickFunctionPitch:
    case stickFunctionYaw:
    case stickFunctionThrottle:
        return RemoteControlCalibrationController::_stickFunctionEnabled(stickFunction);
    case stickFunctionMax:
        qCWarning(JoystickConfigControllerLog) << "Invalid stick function requested: stickFunctionMax";
        break;
    }

    return false;
}
