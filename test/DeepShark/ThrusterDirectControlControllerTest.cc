#include "ThrusterDirectControlControllerTest.h"

#include "ThrusterDirectControlController.h"
#include "QGCCorePlugin.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

void ThrusterDirectControlControllerTest::_happyPathRestoresOriginalFunction()
{
    ThrusterDirectControlController controller;
    controller.setTimingsForTest(1, 100, 100);
    QSignalSpy functionWrites(&controller, &ThrusterDirectControlController::functionWriteRequested);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    QSignalSpy recoveryCompleted(&controller, &ThrusterDirectControlController::recoveryCompleted);

    QVERIFY(controller.beginTest(3, 33, 1600, 1500, 5));
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
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::WaitingRestore, 100);
    QTRY_COMPARE_WITH_TIMEOUT(controller.state(), ThrusterDirectControlController::RecoveryNeeded, 100);
    QCOMPARE(recoveryRequired.count(), 1);
    QVERIFY(recoveryRequired.constFirst().constFirst().toString().contains(QStringLiteral("写回确认超时")));
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
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingRestore);
    QCOMPARE(functionWrites.count(), 2);
    QCOMPARE(functionWrites.at(1).at(0).toInt(), 36);
    QCOMPARE(pwmRequests.count(), 0);

    controller.observeParameterState(36, false);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(recoveryCompleted.count(), 1);
    QCOMPARE(recoveryCompleted.constFirst().at(1).toString(), QStringLiteral("vehicle switched"));
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
    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/Controls/DeepSharkThrusterMappingTool.qml")),
                            QQmlComponent::PreferSynchronous);

    QStringList errors;
    for (const QQmlError &error : component.errors()) {
        errors.append(error.toString());
    }
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(errors.join(QLatin1Char('\n'))));
}

UT_REGISTER_TEST(ThrusterDirectControlControllerTest, TestLabel::Unit)
