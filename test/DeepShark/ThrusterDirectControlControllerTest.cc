#include "ThrusterDirectControlControllerTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "QGCCorePlugin.h"
#include "ThrusterDirectControlController.h"
#include "VehicleTypes.h"

namespace {

// Output reports of the vehicle, each later on its clock than every report before it.
void reportOutput(ThrusterDirectControlController& controller, int pwm, int reports = 2)
{
    static quint32 vehicleTimeUsec = 0;
    for (int report = 0; report < reports; ++report) {
        vehicleTimeUsec += 500000;
        controller.observeOutputReport(vehicleTimeUsec, pwm);
    }
}

}  // namespace

void ThrusterDirectControlControllerTest::_happyPathRestoresOriginalFunction()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 100, 100);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    reportOutput(controller, 1500);
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
    reportOutput(controller, 1600, 1);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 2, 100);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.at(1).at(1).toInt(), 1500);

    // The accepted neutral is not the end: the vehicle has to report the output itself at neutral, twice.
    controller.handleCommandResult(0);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    reportOutput(controller, 1500, 1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    reportOutput(controller, 1500, 1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(functionWrites.at(1).at(0).toInt(), 33);

    controller.observeParameterState(33, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(recoveryCompleted.constFirst().at(0).toInt(), 3);
    QVERIFY(recoveryCompleted.constFirst().at(1).toString().isEmpty());
    QVERIFY(!controller.busy());
}

void ThrusterDirectControlControllerTest::_rejectedTestCommandEndsThroughNeutral()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    reportOutput(controller, 1500);
    QVERIFY(controller.beginTest(4, 34, 1580, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);

    // A reply names the command only. This refusal can be the late reply to an earlier command while the
    // test PWM of this session is on the output, so the function is not written back without neutral.
    controller.handleCommandResult(1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(pwmRequests.at(1).at(1).toInt(), 1500);
    QCOMPARE(functionWrites.count(), 1);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(functionWrites.count(), 1);
    // The output reports neutral, as it did before the test. Nothing shows that the test PWM is not still
    // on its way, so the session does not finish on its own.
    reportOutput(controller, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);
    QCOMPARE(controller.unconfirmedOutputForTest(), 4);
}

void ThrusterDirectControlControllerTest::_commandAndRestoreTimeoutRequireRecovery()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 60, 30);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Testing);
    reportOutput(controller, 1570, 1);
    controller.finishTest();
    QCOMPARE(pwmRequests.count(), 2);

    // The vehicle never retries this command, and the output keeps the test PWM until a neutral arrives.
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 3);
    QCOMPARE(pwmRequests.at(2).at(0).toInt(), 5);
    QCOMPARE(pwmRequests.at(2).at(1).toInt(), 1500);

    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
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

void ThrusterDirectControlControllerTest::_acceptedNeutralAloneDoesNotConfirm()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 200, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    reportOutput(controller, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    // The test PWM gets no reply. It may still have been applied, and its reply may still arrive.
    controller.handleCommandResult(MAV_RESULT_FAILED, VehicleTypes::MavCmdResultFailureNoResponseToCommand);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.count(), 2);

    // A reply carries the command number only. This "accepted" can be the late reply to the test PWM, or to
    // a command of an earlier session, while the neutral itself was lost. The output still reports the test
    // value, so nothing is written back, however many replies arrive.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    reportOutput(controller, 1570, 3);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(pwmRequests.count(), 2);

    // The wait ends without confirmation, and neutral is sent again.
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 3, 2000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    QCOMPARE(pwmRequests.at(2).at(1).toInt(), 1500);
    QCOMPARE(functionWrites.count(), 1);

    // Reports made before the reply do not count, whatever they show.
    reportOutput(controller, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    reportOutput(controller, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    controller.observeParameterState(35, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
}

void ThrusterDirectControlControllerTest::_unconfirmedOutputStaysWithinTheNeutralBudget()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 200, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    reportOutput(controller, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.finishTest();
    // Every neutral is accepted, and the output never reports neutral: the replies are not replies to
    // these neutrals. The session sends its four neutrals, then keeps the output for manual recovery.
    for (int neutral = 1; neutral <= 4; ++neutral) {
        QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1 + neutral, 2000);
        QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingNeutralAck);
        controller.handleCommandResult(MAV_RESULT_ACCEPTED);
        QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
        reportOutput(controller, 1570);
    }
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::RecoveryNeeded, 2000);
    QCOMPARE(pwmRequests.count(), 5);
    QCOMPARE(recoveryRequired.count(), 1);
    QVERIFY(recoveryRequired.constFirst().constFirst().toString().contains(QStringLiteral("没有上报")));
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
}

void ThrusterDirectControlControllerTest::_outputReportsCountOncePerVehicleTime()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    controller.observeOutputReport(1000000, 1500);
    controller.observeOutputReport(1500000, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.observeOutputReport(2000000, 1570);
    controller.finishTest();
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);

    // One report that is delivered twice is one report.
    controller.observeOutputReport(2500000, 1500);
    controller.observeOutputReport(2500000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);

    // A second, slower link delivers reports from before the test, when the output was at neutral. They are
    // older than what was already taken. They neither count nor move the mark back, so that those after
    // them would count; however many arrive, the output stays unconfirmed.
    for (const quint32 oldTime : {1100000u, 1200000u, 1300000u, 1400000u}) {
        controller.observeOutputReport(oldTime, 1500);
    }
    controller.observeOutputReport(2500000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);

    // A report that is not at neutral starts the count again.
    controller.observeOutputReport(3000000, 1570);
    controller.observeOutputReport(3500000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    controller.observeOutputReport(4000000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(pwmRequests.count(), 2);
}

void ThrusterDirectControlControllerTest::_vehicleClockWrapIsNotAStepBack()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);

    // The vehicle's clock has 32 bits of microseconds and is about to wrap.
    controller.observeOutputReport(4293000000u, 1500);
    controller.observeOutputReport(4293500000u, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.observeOutputReport(4294000000u, 1570);
    controller.finishTest();
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);

    controller.observeOutputReport(4294500000u, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    // 4294500000 + 500000 microseconds is 32704 after the wrap. That is the next report, not an old one.
    controller.observeOutputReport(32704, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
}

void ThrusterDirectControlControllerTest::_restartedVehicleClockIsNotTrustedDuringASession()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 200, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    controller.observeOutputReport(1000000000u, 1500);
    controller.observeOutputReport(1000500000u, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.observeOutputReport(1001000000u, 1570);
    controller.finishTest();
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);

    // Reports with a far earlier time: a vehicle that restarted, or an old stream. Which of the two cannot
    // be told from here, so during a session they confirm nothing.
    controller.observeOutputReport(1000000, 1500);
    controller.observeOutputReport(1500000, 1500);
    controller.observeOutputReport(2000000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(functionWrites.count(), 1);
    // The session runs out of neutrals and keeps the output for manual recovery.
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::RecoveryNeeded, 5000);
    QCOMPARE(recoveryRequired.count(), 1);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);

    // Waiting does not make the earlier clock the vehicle's: it could just as well be an old stream.
    controller.setOutputReportMaxAgeForTest(20);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outputReportsAvailable(), 1000);
    controller.observeOutputReport(2500000, 1500);
    controller.observeOutputReport(3000000, 1500);
    QVERIFY(!controller.outputReportsAvailable());
    // It is taken up when the reports are known to start anew: the link was lost and came back, or they
    // come from another link. The first report then places the clock, the next one shows that it runs.
    controller.changeOutputReportSourceForTest();
    controller.setOutputReportMaxAgeForTest(5000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    controller.observeOutputReport(2500000, 1500);
    QVERIFY(!controller.outputReportsAvailable());
    controller.observeOutputReport(3000000, 1500);
    QVERIFY(controller.outputReportsAvailable());

    QVERIFY(controller.beginRecovery(5, 35, QStringLiteral("manual recovery"), 1500));
    controller.observeParameterState(0, false);
    const int neutralsBefore = pwmRequests.count();
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), neutralsBefore + 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.observeOutputReport(3500000, 1500);
    controller.observeOutputReport(4000000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
}

void ThrusterDirectControlControllerTest::_implausibleClockJumpEndsTheConfirmation()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    controller.observeOutputReport(3000000000u, 1500);
    controller.observeOutputReport(3000500000u, 1500);
    QVERIFY(controller.beginTest(5, 35, 1570, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 1000);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    controller.observeOutputReport(3001000000u, 1570);
    controller.finishTest();
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);

    // On a 32-bit clock a time this far back is, by the clock's own order, twenty minutes ahead. The vehicle's
    // clock did not run twenty minutes in this instant, so the report is from another run of the clock. What
    // was seen of the test before it cannot be combined with reports after it: the session stops confirming.
    controller.observeOutputReport(1000000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(recoveryRequired.count(), 1);
    QVERIFY(recoveryRequired.constFirst().constFirst().toString().contains(QStringLiteral("跳变")));
    controller.observeOutputReport(1500000, 1500);
    controller.observeOutputReport(2000000, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
}

void ThrusterDirectControlControllerTest::_waitingDoesNotQualifyAnOlderStream()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);

    controller.observeOutputReport(9500000, 1500);
    controller.observeOutputReport(10000000, 1500);
    QVERIFY(controller.outputReportsAvailable());
    // The reports stop for a while, with nothing running.
    controller.setOutputReportMaxAgeForTest(20);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outputReportsAvailable(), 1000);

    // An old stream arrives: a whole earlier test, from neutral to the test PWM and back. If the pause
    // had made it acceptable, it would let a new test start and then stand in for that test's own reports.
    controller.observeOutputReport(1000000, 1500);
    controller.observeOutputReport(1500000, 1500);
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(controller.outputReportProblem().contains(QStringLiteral("还不能")));
    QVERIFY(!controller.beginTest(5, 0, 1550, 1500, 1000));
    controller.observeOutputReport(2000000, 1550);
    controller.observeOutputReport(2500000, 1500);
    controller.observeOutputReport(3000000, 1500);
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(!controller.beginTest(5, 0, 1550, 1500, 1000));
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(functionWrites.count(), 0);

    // The vehicle's own reports, later than the last ones taken, are usable again at once.
    controller.observeOutputReport(10500000, 1500);
    QVERIFY(controller.outputReportsAvailable());
}

void ThrusterDirectControlControllerTest::_testNeedsReportsWhoseClockMovesOn()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);

    // No reports: the test could be started but never be confirmed as ended.
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(controller.outputReportProblem().contains(QStringLiteral("没有收到")));
    QVERIFY(!controller.beginTest(3, 33, 1600, 1500, 1000));

    // Reports whose time stands still are one report delivered again and again.
    for (int report = 0; report < 4; ++report) {
        controller.observeOutputReport(700, 1500);
    }
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(controller.outputReportProblem().contains(QStringLiteral("还不能")));
    QVERIFY(!controller.beginTest(3, 33, 1600, 1500, 1000));
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(functionWrites.count(), 0);

    controller.observeOutputReport(500700, 1500);
    QVERIFY(controller.outputReportsAvailable());
    QVERIFY(controller.outputReportProblem().isEmpty());
    QVERIFY(controller.beginTest(3, 33, 1600, 1500, 1000));
    QCOMPARE(functionWrites.count(), 1);
}

void ThrusterDirectControlControllerTest::_reportsThatStopBeforeTheTestPwmCancelTheTest()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    reportOutput(controller, 1500);
    QVERIFY(controller.beginTest(3, 33, 1600, 1500, 1000));
    // The reports stop while the function is being disabled. No PWM has been sent, so the function is
    // simply written back and the test does not take place.
    controller.setOutputReportMaxAgeForTest(20);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.outputReportsAvailable(), 1000);
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::WaitingRestore, 1000);
    QCOMPARE(pwmRequests.count(), 0);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(functionWrites.at(1).at(0).toInt(), 33);
    controller.observeParameterState(33, false);
    QCOMPARE(recoveryCompleted.count(), 1);
    QVERIFY(recoveryCompleted.constFirst().at(1).toString().contains(QStringLiteral("上报中断")));
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

    reportOutput(controller, 1500);
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
    // The history of this output holds PWM commands without a reply, so an accepted reply is not trusted
    // on its own. The output stays remembered until the vehicle reports it at neutral.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(controller.unconfirmedOutputForTest(), 5);
    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
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

void ThrusterDirectControlControllerTest::_unseenTestPwmIsNotFinishedByNeutralReports()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);

    // The original function is "disabled": writing it back sends nothing that would end a test PWM, and the
    // vehicle keeps accepting PWM commands for the output afterwards.
    reportOutput(controller, 1500);
    QVERIFY(controller.beginTest(3, 0, 1550, 1500, 1000));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 1, 100);
    // The test PWM is held up on its way. A reply of an earlier command is taken for its reply.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Testing);
    reportOutput(controller, 1500, 2);
    controller.finishTest();
    QCOMPARE(pwmRequests.count(), 2);
    QCOMPARE(pwmRequests.at(1).at(1).toInt(), 1500);
    // The neutral is lost, and another late reply is taken for its reply. The vehicle truthfully reports
    // the output at neutral, where it has been all along.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    reportOutput(controller, 1500, 2);

    // The reports never showed the test PWM, so they do not show that it is over. The function is not
    // written back and the output is not forgotten.
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(recoveryRequired.count(), 1);
    QVERIFY(recoveryRequired.constFirst().constFirst().toString().contains(QStringLiteral("没有上报过测试值")));
    QCOMPARE(functionWrites.count(), 1);
    QCOMPARE(recoveryCompleted.count(), 0);
    QCOMPARE(controller.unconfirmedOutputForTest(), 3);

    // Now the test PWM arrives. The session is still waiting for a recovery, which ends it properly.
    reportOutput(controller, 1550, 2);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QVERIFY(controller.beginRecovery(3, 0, QStringLiteral("manual recovery"), 1500));
    controller.observeParameterState(0, false);
    QTRY_COMPARE_WITH_TIMEOUT(pwmRequests.count(), 3, 100);
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    reportOutput(controller, 1550, 1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    reportOutput(controller, 1500, 2);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    controller.observeParameterState(0, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
}

void ThrusterDirectControlControllerTest::_abortWaitsForPendingCommandResult()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 1000, 1000);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);

    reportOutput(controller, 1500);
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

    // A recovery may start without usable output reports; it just cannot finish without them.
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(controller.beginRecovery(5, 0, QStringLiteral("manual recovery"), 1520));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingDisable);
    reportOutput(controller, 1500);
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
    // The history of this output holds PWM commands without a reply, so an accepted reply is not trusted
    // on its own. The function is written back once the vehicle reports the output at neutral.
    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(pwmRequests.count(), 5);
    QCOMPARE(functionWrites.count(), 2);
    reportOutput(controller, 1520);
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

    controller.handleCommandResult(MAV_RESULT_ACCEPTED);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(pwmRequests.count(), 1);
    QCOMPARE(functionWrites.count(), 1);
    // A report at another value than the neutral of this recovery confirms nothing.
    reportOutput(controller, 1500);
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    reportOutput(controller, 1520);
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

    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
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

    reportOutput(controller, 1500);
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
