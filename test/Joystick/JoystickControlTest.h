#pragma once

#include "VehicleTestManualConnect.h"

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
    void _reassigningAHeldActionReleasesIt();
};
