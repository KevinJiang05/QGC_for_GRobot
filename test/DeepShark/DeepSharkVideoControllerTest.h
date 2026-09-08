#pragma once

#include "UnitTest.h"

class DeepSharkVideoControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _pendingAutoStartIsCancelled();
    void _clearingUriCancelsPendingAutoStart();
    void _failureSignalIsIndependentFromDisplayText();
    void _restartRebuildsVideoSink();
    void _failedRestartCanScheduleAgain();
    void _startedRestartCanScheduleAgain();
    void _flyViewStatusQmlLoads();
};
