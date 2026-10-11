#pragma once

#include <QtCore/QPointer>
#include <memory>

#include "VehicleTestManualConnect.h"

class Joystick;
class MockJoystick;

class JoystickControlTest : public VehicleTestManualConnect
{
    Q_OBJECT

private slots:
    void _manualControlPackets_data();
    void _manualControlPackets();
    void _firmwareButtonOwnership();
    void _configurationKeepsManualControl();
    void _buttonActionEditsAreSerializedWithPolling();
    void _pollingStateChangesAreSerializedWithPolling();
    void _reassignmentSettlesOnlyWhatTheButtonHolds();
    void _guardIsReadWhenAButtonIsEdited();
    void _leavingAVehicleSettlesWhatTheJoystickStarted();
    void _actionsQueuedForAnEarlierSessionDoNotRun();
    void _heldActionIsNotSettledOnAnotherDevice();
    void _releaseWithoutADispatchedPressIsNotCompleted();
    void _releaseOfAPressFromAnEarlierSessionIsNotCompleted();

private:
    /// Connects an ArduSub mock vehicle and lets a calibrated mock joystick control it.
    void _startLiveJoystick(const QString &name, std::unique_ptr<MockJoystick> &mock, QPointer<Joystick> &joystick);
    void _stopLiveJoystick(std::unique_ptr<MockJoystick> &mock, const QPointer<Joystick> &joystick);
    static bool _scanHasSeen(Joystick *joystick, int button, bool pressed);
};
