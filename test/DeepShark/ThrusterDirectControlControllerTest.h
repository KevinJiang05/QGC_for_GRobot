#pragma once

#include "UnitTest.h"

class ThrusterDirectControlControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _happyPathRestoresOriginalFunction();
    void _rejectedTestCommandEndsThroughNeutral();
    void _commandAndRestoreTimeoutRequireRecovery();
    void _lostNeutralIsSentAgain();
    void _neutralAttemptsAreBounded();
    void _duplicateNeutralGoesStraightToRecovery();
    void _recoveryRemembersItsOutput();
    void _confirmedNeutralForgetsTheOutput();
    void _acceptedNeutralAloneDoesNotConfirm();
    void _unconfirmedOutputStaysWithinTheNeutralBudget();
    void _outputReportsCountOncePerVehicleTime();
    void _vehicleClockWrapIsNotAStepBack();
    void _restartedVehicleClockIsNotTrustedDuringASession();
    void _implausibleClockJumpEndsTheConfirmation();
    void _waitingDoesNotQualifyAnOlderStream();
    void _testNeedsReportsWhoseClockMovesOn();
    void _reportsThatStopBeforeTheTestPwmCancelTheTest();
    void _abortRestoresBeforeOutputStarts();
    void _unseenTestPwmIsNotFinishedByNeutralReports();
    void _abortWaitsForPendingCommandResult();
    void _recoveryConfirmsNeutralBeforeParameterRestore();
    void _abortDuringRecoverySettlingStillConfirmsNeutral();
    void _recoveryOfAnOutputThatKeepsTakingPwmNeedsTheOperator();
    void _elapsedTestWaitsForAckWithoutRestartingDuration();
    void _parameterFailureKeepsRecoveryRequired();
    void _inProgressDoesNotCompleteCommand();
    void _originalBackupPrecedesFunctionWrite();
    void _rejectsInvalidSessionInputs();
    void _qmlAdapterLoads();
};
