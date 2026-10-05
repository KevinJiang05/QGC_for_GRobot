#pragma once

#include "UnitTest.h"

class AIDetectionReceiverTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _acceptsBoundedPacketAndExpiresIt();
    void _rejectsOversizedAndStalePackets();
    void _disableClearsDetections();
    void _overlaySwitchMigratesLegacyVideoSetting();
    void _qmlComponentsLoad();
};
