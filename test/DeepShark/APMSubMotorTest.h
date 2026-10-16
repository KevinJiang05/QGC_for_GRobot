#pragma once

#include "BaseClasses/VehicleTestManualConnect.h"

class APMSubMotorTest : public VehicleTestManualConnect
{
    Q_OBJECT

private slots:
    void _manualTestStates_data();
    void _manualTestStates();
};
