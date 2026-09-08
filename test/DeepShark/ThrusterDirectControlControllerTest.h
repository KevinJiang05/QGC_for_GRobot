#pragma once

#include "UnitTest.h"

class ThrusterDirectControlControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _happyPathRestoresOriginalFunction();
    void _rejectedTestCommandRestoresWithoutTesting();
    void _commandAndRestoreTimeoutRequireRecovery();
    void _abortRestoresBeforeOutputStarts();
    void _rejectsInvalidSessionInputs();
    void _qmlAdapterLoads();
};
