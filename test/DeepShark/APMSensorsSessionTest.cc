#include "APMSensorsSessionTest.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QScopeGuard>
#include <QtStateMachine/QStateMachine>
#include <QtTest/QSignalSpy>

#include "APMSensorsComponentController.h"
#include "AppSettings.h"
#include "BulkRefreshJob.h"
#include "GimbalController.h"
#include "MAVLinkProtocol.h"
#include "ParameterManager.h"
#include "QGCStateMachine.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

namespace {
bool parameterWorkFinished(ParameterManager* manager)
{
    // A bulk job may be between retry rounds while all of its individual machines are
    // temporarily idle; keep the link alive until the job itself has completed as well.
    for (auto* child : manager->children()) {
        if (dynamic_cast<BulkRefreshJob*>(child)) {
            return false;
        }
    }
    for (const auto* machine : manager->findChildren<QStateMachine*>()) {
        if (machine->isRunning()) {
            return false;
        }
    }
    return !manager->pendingWrites();
}
}  // namespace

UT_REGISTER_TEST(APMSensorsSessionTest, TestLabel::Integration, TestLabel::Vehicle)

void APMSensorsSessionTest::init()
{
    bool initialized = false;
    const auto cleanupFailedInit = qScopeGuard([&]() {
        if (!initialized) {
            cleanup();
        }
    });
    VehicleTestManualConnect::init();
    if (QTest::currentTestFailed()) {
        return;
    }
    // Test objects are statically registered before QApplication exists. Construct and
    // destroy Qt Quick objects within each fixture's application lifetime instead.
    _next = std::make_unique<QQuickItem>();
    _cancel = std::make_unique<QQuickItem>();
    _progress = std::make_unique<QQuickItem>();
    _help = std::make_unique<QQuickItem>();
    _log = std::make_unique<APMSensorsSessionStatusLog>();
    _dropCalibrationFrames = true;
    _droppedCalibrationFrames = 0;
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    connect(_mockLink, &QObject::destroyed, this, [this]() { _mockLink = nullptr; });
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(_vehicle);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->parameterManager()->parametersReady(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->isInitialConnectComplete(), 10000);

    // Drop only calibration frames before the simulated firmware receives them. Heartbeats,
    // parameter refresh and all other traffic keep using the real MockLink/Vehicle pipeline.
    // The production connection uses a pointer-to-member slot; the string-slot overload
    // cannot match that functor connection. Disconnect this signal's self receiver only.
    QVERIFY(QObject::disconnect(_mockLink, &MockLink::writeBytesQueuedSignal, _mockLink, nullptr));
    _wireFilter =
        connect(_mockLink, &MockLink::writeBytesQueuedSignal, this,
                [this, link = QPointer<MockLink>(_mockLink)](const QByteArray& bytes) {
                    if (!link || !link->isConnected()) {
                        return;
                    }
                    mavlink_message_t receiveBuffer{};
                    mavlink_status_t receiveStatus{};
                    mavlink_message_t message{};
                    mavlink_status_t status{};
                    int decodedFrames = 0;
                    bool drop = false;
                    for (const char byte : bytes) {
                        if (mavlink_frame_char_buffer(&receiveBuffer, &receiveStatus, static_cast<uint8_t>(byte),
                                                      &message, &status) != MAVLINK_FRAMING_OK) {
                            continue;
                        }
                        ++decodedFrames;
                        if (_dropCalibrationFrames && (message.msgid == MAVLINK_MSG_ID_COMMAND_LONG)) {
                            mavlink_command_long_t command{};
                            mavlink_msg_command_long_decode(&message, &command);
                            if (command.command == MAV_CMD_PREFLIGHT_CALIBRATION) {
                                ++_droppedCalibrationFrames;
                                drop = true;
                            }
                        }
                    }
                    QVERIFY2(decodedFrames == 1, "A Vehicle MockLink write must contain one complete MAVLink frame");
                    if (!drop) {
                        // Keep every non-dropped production frame byte-for-byte unchanged.
                        QVERIFY(QMetaObject::invokeMethod(link, "_writeBytesQueued", Qt::QueuedConnection,
                                                          Q_ARG(QByteArray, bytes)));
                    }
                });
    initialized = true;
}

void APMSensorsSessionTest::cleanup()
{
    bool parametersFinished = true;
    if (_mockLink && _vehicle && _mockLink->isConnected()) {
        // Calibration cleanup starts a real parameter refresh. Preserve the forwarding
        // path until those requests finish rather than disconnecting underneath them.
        parametersFinished =
            QTest::qWaitFor([&]() { return parameterWorkFinished(_vehicle->parameterManager()); }, 10000);
    }
    disconnect(_wireFilter);
    _wireFilter = {};
    VehicleTestManualConnect::cleanup();
    _log.reset();
    _help.reset();
    _progress.reset();
    _cancel.reset();
    _next.reset();
    QVERIFY2(parametersFinished, "Calibration parameter work did not finish while the MockLink was connected");
}

std::unique_ptr<APMSensorsComponentController> APMSensorsSessionTest::_createController()
{
    auto controller = std::make_unique<APMSensorsComponentController>();
    controller->_nextButton = _next.get();
    controller->_cancelButton = _cancel.get();
    controller->_progressBar = _progress.get();
    controller->_orientationCalAreaHelpText = _help.get();
    controller->_statusLog = _log.get();
    connect(controller.get(), &APMSensorsComponentController::calibrationActiveChanged, controller.get(), [this]() {
        // One real calibration refresh queues 144 parameter reads on this Debug fixture.
        // Use the production response budget instead of ParameterManager's 50 ms unit-test
        // shortcut, before the queued states enter their waits. Timeouts still fail the test.
        for (auto* machine :
             _vehicle->parameterManager()->findChildren<QGCStateMachine*>(QStringLiteral("PARAM_REQUEST_READ"))) {
            machine->setTimeoutOverride(QStringLiteral("WaitForParamResponseState"),
                                        ParameterManager::kWaitForParamValueAckMs);
        }
    });
    return controller;
}

void APMSensorsSessionTest::_startCalibration(APMSensorsComponentController& controller, int kind)
{
    switch (kind) {
        case 0:
            controller.calibratePressure();
            break;
        case 1:
            controller.calibrateGyro();
            break;
        case 2:
            controller.levelHorizon();
            break;
        case 3:
            controller.calibrateAccel(true);
            break;
        case 4:
            controller.calibrateAccel(false);
            break;
        default:
            QFAIL("Unknown calibration test kind");
    }
}

void APMSensorsSessionTest::_sendAck(MAV_RESULT result)
{
    _mockLink->sendUnexpectedCommandAck(MAV_CMD_PREFLIGHT_CALIBRATION, result);
}

void APMSensorsSessionTest::_missingStartCanEndLocalWait_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("pressure") << 0;
    QTest::newRow("gyro") << 1;
    QTest::newRow("level") << 2;
    QTest::newRow("simple-accel") << 3;
    QTest::newRow("full-accel") << 4;
}

void APMSensorsSessionTest::_missingStartCanEndLocalWait()
{
    QFETCH(int, kind);
    auto controller = _createController();
    QSignalSpy complete(controller.get(), &APMSensorsComponentController::calibrationComplete);
    QSignalSpy buttons(controller.get(), &APMSensorsComponentController::setAllCalButtonsEnabled);
    _startCalibration(*controller, kind);
    QVERIFY(controller->calibrationActive());
    QVERIFY(_cancel->isEnabled());
    QCOMPARE(controller->_calibrationWaitTimer.interval(), kind == 4 ? 120000 : 30000);
    QTRY_COMPARE_WITH_TIMEOUT(_droppedCalibrationFrames, 1, 1000);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_PREFLIGHT_CALIBRATION), 0);

    // Advance this controller's real timer; expiry must not claim success or stop a slow cal.
    controller->_calibrationWaitTimer.setInterval(1);
    QTRY_VERIFY_WITH_TIMEOUT(controller->calibrationTimedOut(), 1000);
    QVERIFY(controller->calibrationActive());
    QCOMPARE(complete.count(), 0);
    controller->endCalibrationWait();
    QVERIFY(!controller->calibrationActive());
    QVERIFY(controller->calibrationStateUnknown());
    QVERIFY(!controller->_calibrationWaitTimer.isActive());
    QVERIFY(_vehicle->vehicleLinkManager()->communicationLostEnabled());
    QCOMPARE(buttons.last().at(0).toBool(), true);
    QCOMPARE(complete.count(), 0);

    // A terminal ACK arriving after local cleanup must not turn the ended session into success.
    QSignalSpy messages(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived);
    _sendAck(MAV_RESULT_ACCEPTED);
    QTRY_VERIFY_WITH_TIMEOUT(messages.count() > 0, 1000);
    QCOMPARE(complete.count(), 0);
    QVERIFY(controller->calibrationStateUnknown());
}

void APMSensorsSessionTest::_cancelWithoutResponseCanEndLocalWait()
{
    auto controller = _createController();
    QSignalSpy complete(controller.get(), &APMSensorsComponentController::calibrationComplete);
    controller->calibratePressure();
    controller->cancelCalibration();
    QVERIFY(controller->_waitingForCancel);
    QVERIFY(controller->calibrationActive());
    QCOMPARE(controller->_calibrationWaitTimer.interval(), 10000);
    controller->_calibrationWaitTimer.setInterval(1);
    QTRY_VERIFY_WITH_TIMEOUT(controller->calibrationTimedOut(), 1000);
    controller->endCalibrationWait();
    QVERIFY(!controller->_waitingForCancel);
    QVERIFY(!controller->calibrationActive());
    QVERIFY(controller->calibrationStateUnknown());
    QVERIFY(_vehicle->vehicleLinkManager()->communicationLostEnabled());
    QCOMPARE(complete.count(), 0);

    // Existing queued cancel retries must finish before another bare start can be sent.
    QVERIFY(_vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_PREFLIGHT_CALIBRATION));
    expectAppMessage(QRegularExpression(QRegularExpression::escape(
        QCoreApplication::translate("APMSensorsComponentController",
                                    "A calibration command is still awaiting a response. Wait for it to finish before "
                                    "starting another calibration."))));
    controller->calibrateGyro();
    QVERIFY(!controller->calibrationActive());
    verifyExpectedLogMessage();
    _sendAck(MAV_RESULT_ACCEPTED);
    QTRY_VERIFY_WITH_TIMEOUT(
        !_vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_PREFLIGHT_CALIBRATION), 1000);
    QCOMPARE(complete.count(), 0);
}

void APMSensorsSessionTest::_startAcceptedAfterCancelIsUnknown_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("pressure") << 0;
    QTest::newRow("gyro") << 1;
    QTest::newRow("level") << 2;
    QTest::newRow("simple-accel") << 3;
}

void APMSensorsSessionTest::_startAcceptedAfterCancelIsUnknown()
{
    QFETCH(int, kind);
    auto controller = _createController();
    QSignalSpy complete(controller.get(), &APMSensorsComponentController::calibrationComplete);
    QSignalSpy buttons(controller.get(), &APMSensorsComponentController::setAllCalButtonsEnabled);
    _startCalibration(*controller, kind);
    QVERIFY(controller->calibrationActive());
    QVERIFY(_cancel->isEnabled());
    // Simple accel keeps monitoring enabled; the other three types pause it until cleanup.
    QCOMPARE(_vehicle->vehicleLinkManager()->communicationLostEnabled(), kind == 3);
    QSignalSpy resetLog(controller.get(), &APMSensorsComponentController::resetStatusTextArea);

    controller->cancelCalibration();
    QVERIFY(controller->_waitingForCancel);
    QCOMPARE(controller->_calibrationWaitTimer.interval(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(_droppedCalibrationFrames, 2, 1000);
    QVERIFY(_vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_PREFLIGHT_CALIBRATION));

    // Deliver the start's delayed result through MAVLink after both requests were sent.
    // It is indistinguishable on the wire from an accepted cancel request.
    _sendAck(MAV_RESULT_ACCEPTED);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->calibrationActive(), 1000);
    QVERIFY(controller->calibrationStateUnknown());
    QVERIFY(!controller->_waitingForCancel);
    QVERIFY(!controller->_calibrationWaitTimer.isActive());
    QVERIFY(_vehicle->vehicleLinkManager()->communicationLostEnabled());
    QCOMPARE(buttons.last().at(0).toBool(), true);
    QVERIFY(!_cancel->isEnabled());
    QVERIFY(!_next->isEnabled());
    QCOMPARE(resetLog.count(), 0);  // The cancelled branch would clear the operator's log.
    QCOMPARE(complete.count(), 0);
    const QString unknown = QCoreApplication::translate(
        "APMSensorsComponentController",
        "Local waiting ended. The vehicle's calibration state is unknown. "
        "Confirm calibration has stopped before starting another calibration or operating the vehicle.");
    QVERIFY(_log->lines.contains(unknown));
}

void APMSensorsSessionTest::_slowCalibrationCanFinishAfterTimeout()
{
    auto controller = _createController();
    QSignalSpy complete(controller.get(), &APMSensorsComponentController::calibrationComplete);
    controller->calibratePressure();
    controller->_calibrationWaitTimer.setInterval(1);
    QTRY_VERIFY_WITH_TIMEOUT(controller->calibrationTimedOut(), 1000);
    QVERIFY(controller->calibrationActive());
    QVERIFY(!_vehicle->vehicleLinkManager()->communicationLostEnabled());
    controller->_calibrationWaitTimer.setInterval(30000);
    _sendAck(MAV_RESULT_IN_PROGRESS);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->calibrationTimedOut(), 1000);
    QVERIFY(controller->calibrationActive());
    _sendAck(MAV_RESULT_ACCEPTED);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->calibrationActive(), 1000);
    QCOMPARE(complete.count(), 1);
    QVERIFY(!controller->calibrationStateUnknown());
    QVERIFY(_vehicle->vehicleLinkManager()->communicationLostEnabled());
}

void APMSensorsSessionTest::_canStartAgainAfterLocalExit()
{
    auto controller = _createController();
    QSignalSpy complete(controller.get(), &APMSensorsComponentController::calibrationComplete);
    controller->calibratePressure();
    controller->_calibrationWaitTimer.setInterval(1);
    QTRY_VERIFY_WITH_TIMEOUT(controller->calibrationTimedOut(), 1000);
    controller->endCalibrationWait();
    QVERIFY(controller->calibrationStateUnknown());
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 10000);
    // No reconnect or persistent interlock is introduced by ending the local wait.
    _dropCalibrationFrames = false;
    controller->calibratePressure();
    QVERIFY(controller->calibrationActive());
    QVERIFY(!controller->calibrationStateUnknown());
    QTRY_VERIFY_WITH_TIMEOUT(!controller->calibrationActive(), 2000);
    QCOMPARE(complete.count(), 1);
}

void APMSensorsSessionTest::_lateAccelResultDoesNotFinishPressureSession()
{
    auto controller = _createController();
    controller->calibrateAccel(false);
    controller->_calibrationWaitTimer.setInterval(1);
    QTRY_VERIFY_WITH_TIMEOUT(controller->calibrationTimedOut(), 1000);
    controller->endCalibrationWait();
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 10000);
    QSignalSpy complete(controller.get(), &APMSensorsComponentController::calibrationComplete);
    controller->calibratePressure();
    QVERIFY(controller->calibrationActive());

    mavlink_message_t staleAccelResult{};
    mavlink_msg_command_long_pack_chan(_vehicle->id(), _vehicle->defaultComponentId(),
                                       _mockLink->outgoingMavlinkChannel(), &staleAccelResult,
                                       MAVLinkProtocol::instance()->getSystemId(), MAVLinkProtocol::getComponentId(),
                                       MAV_CMD_ACCELCAL_VEHICLE_POS, 0, ACCELCAL_VEHICLE_POS_SUCCESS, 0, 0, 0, 0, 0, 0);
    QSignalSpy messages(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived);
    _mockLink->respondWithMavlinkMessage(staleAccelResult);
    QTRY_VERIFY_WITH_TIMEOUT(messages.count() > 0, 1000);
    QVERIFY(controller->calibrationActive());
    QCOMPARE(complete.count(), 0);
    _sendAck(MAV_RESULT_ACCEPTED);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->calibrationActive(), 1000);
    QCOMPARE(complete.count(), 1);
}

void APMSensorsSessionTest::_destructionRestoresOriginalMonitoring_data()
{
    QTest::addColumn<bool>("originallyEnabled");
    QTest::newRow("page-owned-pause") << true;
    QTest::newRow("already-paused") << false;
}

void APMSensorsSessionTest::_destructionRestoresOriginalMonitoring()
{
    QFETCH(bool, originallyEnabled);
    auto* const manager = _vehicle->vehicleLinkManager();
    manager->setCommunicationLostEnabled(originallyEnabled);
    const auto restore = qScopeGuard([&]() {
        if (_mockLink) {
            _mockLink->setCommLost(false);
        }
        manager->setCommunicationLostEnabled(true);
    });
    QPointer<Vehicle> oldVehicle = _vehicle;
    auto controller = _createController();
    controller->calibratePressure();
    QVERIFY(!manager->communicationLostEnabled());
    controller.reset();
    QVERIFY(oldVehicle);
    QCOMPARE(manager->communicationLostEnabled(), originallyEnabled);
    if (originallyEnabled) {
        // This ArduSub Mock has no gimbal. Isolate its unrelated background discovery,
        // then finish requests already queued before testing real heartbeat-loss detection.
        QVERIFY(_vehicle->gimbalController());
        QVERIFY(QObject::disconnect(_vehicle, &Vehicle::mavlinkMessageReceived, _vehicle->gimbalController(), nullptr));
        const auto discoveryRequestsFinished = [this]() {
            for (int component = 0; component <= 255; ++component) {
                if (_vehicle->isMavCommandPending(component, MAV_CMD_REQUEST_MESSAGE)) {
                    return false;
                }
            }
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(discoveryRequestsFinished(), 5000);
        _mockLink->setCommLost(true);
        QTRY_VERIFY_WITH_TIMEOUT(manager->communicationLost(), VehicleLinkManager::kTestCommLostDetectionTimeoutMs);
        QVERIFY(oldVehicle);
    }
}

void APMSensorsSessionTest::_vehicleDestroyedBeforeController()
{
    auto controller = _createController();
    controller->calibratePressure();
    QPointer<Vehicle> original = _vehicle;
    _disconnectMockLink();
    QTRY_VERIFY_WITH_TIMEOUT(original.isNull(), 5000);
    controller.reset();
}

void APMSensorsSessionTest::_armedOverrideStillRejected()
{
    auto* const setting = SettingsManager::instance()->appSettings()->disableSetupSafetyRestrictions();
    const QVariant previous = setting->rawValue();
    const auto restore = qScopeGuard([&]() { setting->setRawValue(previous); });
    setting->setRawValue(true);
    QVERIFY(_vehicle->setupSafetyRestrictionsDisabled());
    _vehicle->setArmed(true, false);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 3000);
    auto controller = _createController();
    expectAppMessage(QRegularExpression(QRegularExpression::escape(QCoreApplication::translate(
        "APMSensorsComponentController", "Disarm the vehicle before calibrating sensors."))));
    controller->calibratePressure();
    verifyExpectedLogMessage();
    QVERIFY(!controller->calibrationActive());
    QCOMPARE(_droppedCalibrationFrames, 0);
    QVERIFY(_vehicle->vehicleLinkManager()->communicationLostEnabled());
    _vehicle->setArmed(false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->armed(), 3000);
}
