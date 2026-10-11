#include "ThrusterDirectControlControllerTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "QGCCorePlugin.h"
#include "ThrusterDirectControlController.h"
#include "VehicleTypes.h"

void ThrusterDirectControlControllerTest::_happyPathRestoresOriginalFunction()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 100, 100);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginTest(3, 33, 1600, 1500, 80));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingDisable);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(functionWrites.constFirst().constFirst().toInt(), 0);

    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingTestAck);
    QCOMPARE(pwmRequests.at(0).at(0).toInt(), 3);
    QCOMPARE(pwmRequests.at(0).at(1).toInt(), 1600);

    controller.handleCommandResult(0);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Testing);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 2, 100);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.at(1).at(1).toInt(), 1500);

    controller.handleCommandResult(0);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(functionWrites.at(1).at(0).toInt(), 33);

    controller.observeParameterState(33, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(recoveryCompleted.constFirst().at(0).toInt(), 3);
    QVERIFY(recoveryCompleted.constFirst().at(1).toString().isEmpty());
    QVERIFY(!controller.busy());
}

void ThrusterDirectControlControllerTest::_rejectedTestCommandRestoresWithoutTesting()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 100, 100);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginTest(4, 34, 1580, 1500, 10));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);

    controller.handleCommandResult(1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(pwmRequests.count(), 1);
    controller.observeParameterState(34, false);

    QCOMPARE(recoveryCompleted.count(), 1);
    QVERIFY(recoveryCompleted.constFirst().at(1).toString().contains(QStringLiteral("拒绝")));
}

void ThrusterDirectControlControllerTest::_commandAndRestoreTimeoutRequireRecovery()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 60, 30);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 10));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);

    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::WaitingNeutralAck, 100);
    QCOMPARE(pwmRequests.count(), 2);
    // Every neutral attempt times out before the session gives up.
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::RecoveryNeeded, 2000);
    QCOMPARE(pwmRequests.count(), 5);
    QCOMPARE(recoveryRequired.count(), 1);
    QVERIFY(recoveryRequired.constFirst().constFirst().toString().contains(QStringLiteral("回中指令无回执")));
    QCOMPARE(controller.originalFunction(), 35);
}

void ThrusterDirectControlControllerTest::_lostNeutralIsSentAgain()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Testing);
    controller.finishTest();
    QCOMPARE(pwmRequests.count(), 2);

    // The vehicle never retries this command, and the output keeps the test PWM until a neutral arrives.
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 3);
    QCOMPARE(pwmRequests.at(2).at(0).toInt(), 5);
    QCOMPARE(pwmRequests.at(2).at(1).toInt(), 1500);

    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    controller.observeParameterState(35, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(recoveryRequired.count(), 0);
}

void ThrusterDirectControlControllerTest::_neutralAttemptsAreBounded()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.finishTest();
    for (int attempt = 0; attempt < 3; ++attempt) {
        QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
        controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    }
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 5);
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(pwmRequests.count(), 5);
    QCOMPARE(recoveryRequired.count(), 1);
    // The original function is not written back while neutral is unconfirmed.
    QCOMPARE(controller.originalFunction(), 35);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
}

void ThrusterDirectControlControllerTest::_duplicateNeutralGoesStraightToRecovery()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.finishTest();
    QCOMPARE(pwmRequests.count(), 2);
    // The queue rejected the command without sending it; repeating at once would only be rejected again.
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureDuplicateCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(recoveryRequired.count(), 1);
}

void ThrusterDirectControlControllerTest::_unansweredTestPwmNeedsTwoAcceptedNeutrals()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    // The test PWM gets no reply. It may still have been applied, and its reply may still arrive.
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);

    // A reply carries the command number only. The first "accepted" during the neutral phase can be the
    // late reply to the test PWM, so it must not be taken as proof of neutral.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 3);
    QCOMPARE(pwmRequests.at(2).at(1).toInt(), 1500);
    QCOMPARE(functionWrites.count(), 1);

    // Only one unanswered non-neutral command exists, so a second accepted neutral cannot be its reply.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    controller.observeParameterState(35, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
}

void ThrusterDirectControlControllerTest::_secondNeutralConfirmationStaysWithinTheBudget()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    // Three neutrals get no reply, the fourth is accepted. Asking for the second confirmation would be a
    // fifth neutral, which the session is not allowed to send.
    for (int attempt = 0; attempt < 3; ++attempt) {
        controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    }
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 5);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(pwmRequests.count(), 5);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(recoveryRequired.count(), 1);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
}

void ThrusterDirectControlControllerTest::_recoveryRemembersItsOutput()
{
    // A recovery taken over from an earlier run: the output may still hold that run's test PWM, long
    // before this controller sends anything itself.
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QCOMPARE(controller.unconfirmedOutputForTest(), -1);
    QVERIFY(controller.beginRecovery(5, 35, QStringLiteral("manual recovery"), 1520));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingDisable);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
}

void ThrusterDirectControlControllerTest::_confirmedNeutralForgetsTheOutput()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);

    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.finishTest();
    for (int attempt = 0; attempt < 4; ++attempt) {
        controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    }
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);

    QVERIFY(controller.beginRecovery(5, 35, QStringLiteral("manual recovery"), 1500));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 6, 1000);
    // The history of this output holds a PWM command without a reply, so the first accepted reply is
    // not trusted on its own and neutral is requested once more.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    // Neutral is confirmed. If the parameter now fails, nothing may send a PWM to this output again.
    QCOMPARE(controller.unconfirmedOutputForTest(), -1);
    controller.reportParameterUnavailable(QStringLiteral("parameter read failed"));
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(controller.unconfirmedOutputForTest(), -1);
}

void ThrusterDirectControlControllerTest::_abortRestoresBeforeOutputStarts()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 100, 100);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginTest(6, 36, 1560, 1500, 10));
    controller.abort(QStringLiteral("vehicle switched"));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingDisable);
    QCOMPARE(functionWrites.count(), 1);
    controller.observeParameterState(0, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(functionWrites.at(1).at(0).toInt(), 36);
    QCOMPARE(pwmRequests.count(), 0);

    controller.observeParameterState(36, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(recoveryCompleted.constFirst().at(1).toString(), QStringLiteral("vehicle switched"));
}

void ThrusterDirectControlControllerTest::_noResponseSendsNeutralBeforeRestoringDisabled()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginTest(3, 0, 1550, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(pwmRequests.at(1).at(1).toInt(), 1500);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);

    // The history of this output holds a PWM command without a reply, so the first accepted reply is
    // not trusted on its own and neutral is requested once more.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 3);
    QCOMPARE(functionWrites.count(), 1);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    controller.observeParameterState(0, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QVERIFY(recoveryCompleted.constFirst().at(1).toString().contains(QStringLiteral("无回执")));
}

void ThrusterDirectControlControllerTest::_abortWaitsForPendingCommandResult()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);

    QVERIFY(controller.beginTest(2, 34, 1550, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    controller.abort(QStringLiteral("stop requested"));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingTestAck);
    QCOMPARE(pwmRequests.count(), 1);
    QVERIFY(!controller.reset());

    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(pwmRequests.at(1).at(1).toInt(), 1500);
}

void ThrusterDirectControlControllerTest::_recoveryConfirmsNeutralBeforeParameterRestore()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginRecovery(5, 0, QStringLiteral("manual recovery"), 1520));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingDisable);
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(pwmRequests.constFirst().at(1).toInt(), 1520);
    // Neutral is repeated, and the function is not written back while none of the attempts is confirmed.
    for (int attempt = 0; attempt < 3; ++attempt) {
        controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
        QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
        QCOMPARE(functionWrites.count(), 1);
    }
    QCOMPARE(pwmRequests.count(), 4);
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(recoveryRequired.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(controller.originalFunction(), 0);
    QVERIFY(!controller.beginTest(5, 0, 1550, 1500, 1000));

    QVERIFY(controller.beginRecovery(5, 0, QStringLiteral("retry"), 1520));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 5, 100);
    // The history of this output holds a PWM command without a reply, so the first accepted reply is
    // not trusted on its own and neutral is requested once more.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 6);
    QCOMPARE(functionWrites.count(), 2);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 3);
    controller.observeParameterState(0, false);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
}

void ThrusterDirectControlControllerTest::_abortDuringRecoverySettlingStillConfirmsNeutral()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginRecovery(5, 35, QStringLiteral("manual recovery"), 1520));
    controller.observeParameterState(0, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::DisableSettling);
    controller.abort(QStringLiteral("user closed"));
    QCOMPARE(controller.state(), ThrusterDirectControlController::DisableSettling);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.constFirst().at(0).toInt(), 5);
    QCOMPARE(pwmRequests.constFirst().at(1).toInt(), 1520);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);

    // The history of this output holds a PWM command without a reply, so the first accepted reply is
    // not trusted on its own and neutral is requested once more.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(functionWrites.count(), 1);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(functionWrites.at(1).at(0).toInt(), 35);
    QCOMPARE(recoveryCompleted.count(), 0);
    controller.observeParameterState(35, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(recoveryCompleted.constFirst().at(1).toString(), QStringLiteral("user closed"));
}

void ThrusterDirectControlControllerTest::_elapsedTestWaitsForAckWithoutRestartingDuration()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);

    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    controller.finishTest();  // The duration timer expired while the first ACK was pending.
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingTestAck);
    QCOMPARE(pwmRequests.count(), 1);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);
}

void ThrusterDirectControlControllerTest::_parameterFailureKeepsRecoveryRequired()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    QVERIFY(controller.beginTest(6, 36, 1550, 1500, 1000));
    controller.reportParameterUnavailable(QStringLiteral("parameter write failed"));
    controller.observeParameterState(0, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(pwmRequests.count(), 0);
    QCOMPARE(recoveryCompleted.count(), 0);
    QCOMPARE(recoveryRequired.count(), 1);
    QCOMPARE(controller.originalFunction(), 36);
}

void ThrusterDirectControlControllerTest::_inProgressDoesNotCompleteCommand()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);

    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    controller.handleCommandResult(MAV_RESULT_IN_PROGRESS);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingTestAck);
    QCOMPARE(pwmRequests.count(), 1);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Testing);
}

void ThrusterDirectControlControllerTest::_originalBackupPrecedesFunctionWrite()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QStringList events;
    connect(&controller, &ThrusterDirectControlController::originalFunctionConfirmed, this,
            [&events](int output, int function) {
                events.append(QStringLiteral("backup:%1:%2").arg(output).arg(function));
            });
    connect(&controller, &ThrusterDirectControlController::functionWriteRequested, this,
            [&events](int function) { events.append(QStringLiteral("write:%1").arg(function)); });

    QVERIFY(controller.beginTest(4, 36, 1550, 1500, 1000));
    QCOMPARE(events, QStringList({QStringLiteral("backup:4:36"), QStringLiteral("write:0")}));
}

void ThrusterDirectControlControllerTest::_rejectsInvalidSessionInputs()
{
    ThrusterDirectControlController controller;
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);

    QVERIFY(!controller.beginTest(0, 33, 1600, 1500, 1000));
    QVERIFY(!controller.beginTest(3, 33, 2500, 1500, 1000));
    QVERIFY(!controller.beginTest(3, 33, 1600, 1500, 6000));
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(functionWrites.count(), 0);
}

void ThrusterDirectControlControllerTest::_qmlAdapterLoads()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(
        &engine, QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/Controls/DeepSharkThrusterMappingTool.qml")),
        QQmlComponent::PreferSynchronous);

    QStringList errors;
    for (const QQmlError& error : component.errors()) {
        errors.append(error.toString());
    }
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(errors.join(QLatin1Char('\n'))));
}

UT_REGISTER_TEST(ThrusterDirectControlControllerTest, TestLabel::Unit)
