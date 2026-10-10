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
    void _noResponseSendsNeutralBeforeRestoringDisabled();
    void _abortWaitsForPendingCommandResult();
    void _recoveryConfirmsNeutralBeforeParameterRestore();
    void _abortDuringRecoverySettlingStillConfirmsNeutral();
    void _elapsedTestWaitsForAckWithoutRestartingDuration();
    void _parameterFailureKeepsRecoveryRequired();
    void _inProgressDoesNotCompleteCommand();
    void _originalBackupPrecedesFunctionWrite();
    void _rejectsInvalidSessionInputs();
    void _qmlAdapterLoads();
};
