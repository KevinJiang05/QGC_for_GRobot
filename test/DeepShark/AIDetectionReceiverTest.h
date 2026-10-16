#pragma once

#include "UnitTest.h"

class AIDetectionManager;

class AIDetectionReceiverTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _acceptsBoundedPacketAndExpiresIt();
    void _rejectsOversizedAndStalePackets();
    void _disableClearsDetections();
    void _overlaySwitchMigratesLegacyVideoSetting();
    void _runningInputChangeStopsSession_data();
    void _runningInputChangeStopsSession();
    void _unchangedInputSettingsKeepSession();
    void _stoppedInputChangesDoNotStart();
    void _qmlComponentsLoad();

private:
    void _startManagedStub(AIDetectionManager* manager);
};
