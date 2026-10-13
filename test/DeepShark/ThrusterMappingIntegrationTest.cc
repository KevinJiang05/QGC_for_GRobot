#include "ThrusterMappingIntegrationTest.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QSettings>
#include <QtGui/QImage>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtStateMachine/QStateMachine>
#include <QtTest/QSignalSpy>
#include <memory>

#include "AppSettings.h"
#include "ColoredSvgImageProvider.h"
#include "Fact.h"
#include "LinkManager.h"
#include "MAVLinkProtocol.h"
#include "MavlinkSettings.h"
#include "ParameterManager.h"
#include "QGCCorePlugin.h"
#include "QGCImageProvider.h"
#include "SettingsManager.h"
#include "ThrusterDirectControlController.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

namespace {

QByteArray encodedMessage(const mavlink_message_t& message)
{
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN]{};
    const int size = mavlink_msg_to_send_buffer(buffer, &message);
    return QByteArray(reinterpret_cast<const char*>(buffer), size);
}

QList<mavlink_command_long_t> sentCommands(const QSignalSpy& sent, MAV_CMD command)
{
    QList<mavlink_command_long_t> commands;
    for (const auto& record : sent) {
        mavlink_message_t receiveBuffer{};
        mavlink_status_t receiveStatus{};
        mavlink_message_t message{};
        mavlink_status_t status{};
        for (const char byte : record.first().toByteArray()) {
            if (mavlink_frame_char_buffer(&receiveBuffer, &receiveStatus, static_cast<uint8_t>(byte), &message,
                                          &status) != MAVLINK_FRAMING_OK ||
                message.msgid != MAVLINK_MSG_ID_COMMAND_LONG) {
                continue;
            }
            mavlink_command_long_t decoded{};
            mavlink_msg_command_long_decode(&message, &decoded);
            if (decoded.command == command) {
                commands.append(decoded);
            }
        }
    }
    return commands;
}

QRegularExpression rebootNotice()
{
    return QRegularExpression(
        QRegularExpression::escape(QCoreApplication::translate("Fact", "Reboot vehicle for changes to take effect.")));
}

QString controllerDiagnostics(ThrusterDirectControlController* controller, MockLink* link)
{
    const Fact* fact = controller->getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION"));
    return QStringLiteral("state=%1 reason=%2 original=%3 fact=%4 mock=%5 pwmCommands=%6 pendingWrites=%7 history=%8")
        .arg(controller->stateName(), controller->failureReason())
        .arg(controller->originalFunction())
        .arg(fact ? fact->rawValue().toString() : QStringLiteral("missing"))
        .arg(link ? link->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toString()
                  : QStringLiteral("disconnected"))
        .arg(link ? link->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO) : -1)
        .arg(controller->vehicle() ? controller->vehicle()->parameterManager()->pendingWrites() : false)
        .arg(controller->property("integrationStateTrace").toStringList().join(QStringLiteral(" -> ")));
}

void traceController(ThrusterDirectControlController* controller, MockLink* link)
{
    const QPointer<MockLink> guardedLink(link);
    QObject::connect(
        controller, &ThrusterDirectControlController::stateChanged, controller, [controller, guardedLink]() {
            QStringList states = controller->property("integrationStateTrace").toStringList();
            states.append(
                QStringLiteral("%1(%2;mock=%3)")
                    .arg(controller->stateName(), controller->failureReason())
                    .arg(guardedLink
                             ? guardedLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION"))
                                   .toString()
                             : QStringLiteral("disconnected")));
            controller->setProperty("integrationStateTrace", states);
        });
}

bool freshMockTraffic(MockLink* link, Vehicle* vehicle)
{
    // Creating a fresh engine can consume the unit-test heartbeat grace period.
    // Wait for a real Mock packet to traverse the protocol before enabling a test.
    QSignalSpy traffic(MAVLinkProtocol::instance(), &MAVLinkProtocol::messageReceived);
    return link && vehicle &&
           QTest::qWaitFor(
               [link, vehicle, &traffic]() {
                   for (const auto& packet : traffic) {
                       if (packet.first().value<LinkInterface*>() == link) {
                           return !vehicle->vehicleLinkManager()->communicationLost();
                       }
                   }
                   return false;
               },
               2000);
}

QQuickItem* visualItem(QQuickItem* item, const QString& objectName)
{
    if (!item) {
        return nullptr;
    }
    if (item->objectName() == objectName) {
        return item;
    }
    for (QQuickItem* child : item->childItems()) {
        if (QQuickItem* found = visualItem(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

QQuickItem* dialogItem(QObject* dialog, const QString& objectName)
{
    return visualItem(qvariant_cast<QQuickItem*>(dialog->property("contentItem")), objectName);
}

bool parameterWorkFinished(ParameterManager* manager)
{
    for (const auto* machine : manager->findChildren<QStateMachine*>()) {
        if (machine->isRunning()) {
            return false;
        }
    }
    return true;
}

// Instantiate the production dialog without booting unrelated flight/video pages.
struct MappingDialogFixture
{
    MappingDialogFixture()
    {
        QGCCorePlugin::instance()->init();
        engine.addImportPath(QStringLiteral("qrc:/qml"));
        engine.addImageProvider(QStringLiteral("QGCImages"), new QGCImageProvider());
        engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
        QQmlComponent windowComponent(&engine);
        windowComponent.setData(R"(
            import QtQuick
            Window {
                width: 1280
                height: 900
                property QtObject globals: QtObject { property int validationErrorCount: 0 }
                function allowViewSwitch() { return true }
            }
        )",
                                QUrl());
        window.reset(qobject_cast<QQuickWindow*>(windowComponent.create()));
        if (!window) {
            error = windowComponent.errorString();
            return;
        }
        engine.rootContext()->setContextProperty(QStringLiteral("mainWindow"), window.get());
        engine.rootContext()->setContextProperty(QStringLiteral("globals"),
                                                 qvariant_cast<QObject*>(window->property("globals")));
        QQmlComponent component(
            &engine, QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/Controls/DeepSharkThrusterMappingTool.qml")),
            QQmlComponent::PreferSynchronous);
        dialog.reset(component.createWithInitialProperties(
            {{QStringLiteral("parent"), QVariant::fromValue(window->contentItem())},
             {QStringLiteral("destroyOnClose"), false}}));
        error = component.errorString();
    }

    ThrusterDirectControlController* controller() const
    {
        return dialog ? dialog->findChild<ThrusterDirectControlController*>(QStringLiteral("thrusterDirectControl"))
                      : nullptr;
    }

    QObject* settings() const
    {
        return dialog ? dialog->findChild<QObject*>(QStringLiteral("thrusterMappingSettings")) : nullptr;
    }

    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QObject> dialog;
    QString error;
};

QString csvText(QObject* dialog)
{
    QVariant result;
    if (!QMetaObject::invokeMethod(dialog, "exportCsvText", Q_RETURN_ARG(QVariant, result))) {
        return {};
    }
    return result.toString();
}

bool outputReportsArrive(ThrusterDirectControlController* controller)
{
    // A controller sees the reports that arrive after it was bound to the vehicle.
    return controller && QTest::qWaitFor([controller]() { return controller->outputReportsAvailable(); }, 3000);
}

QString portName(QObject* dialog, int output)
{
    QVariant result;
    if (!QMetaObject::invokeMethod(dialog, "portName", Q_RETURN_ARG(QVariant, result),
                                   Q_ARG(QVariant, QVariant(output)))) {
        return {};
    }
    return result.toString();
}

}  // namespace

void ThrusterMappingIntegrationTest::_connectFixture(quint64 uid)
{
    _fixtureUid = uid;
    _servoAckPolicy = AcceptServoAck;
    _holdArmingUpdates = false;
    _holdArmingHeartbeats = false;
    _lostServoPwm = -1;
    _heldServoPwm = -1;
    _heldServoCommands.clear();
    _mockOutputs.fill(1500);
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    connect(_mockLink, &QObject::destroyed, this, [this]() { _mockLink = nullptr; });
    _commandWatch = connect(_mockLink, &MockLink::writeBytesQueuedSignal, this,
                            &ThrusterMappingIntegrationTest::_applyServoCommands);
    // Only this owned MockLink is intercepted. Parameter writes/reads and commands
    // still traverse the real protocol, Vehicle and command queue.
    QVERIFY(QObject::disconnect(_mockLink, &LinkInterface::bytesReceived, MAVLinkProtocol::instance(),
                                &MAVLinkProtocol::receiveBytes));
    _responseFilter = connect(_mockLink, &LinkInterface::bytesReceived, this,
                              &ThrusterMappingIntegrationTest::_filterMockResponse, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->isInitialConnectComplete(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->parameterManager()->parametersReady(), 10000);
    QCOMPARE(_vehicle->vehicleUID(), uid);
}

void ThrusterMappingIntegrationTest::_applyServoCommands(const QByteArray& bytes)
{
    mavlink_message_t receiveBuffer{};
    mavlink_status_t receiveStatus{};
    mavlink_message_t message{};
    mavlink_status_t status{};
    for (const char byte : bytes) {
        if (mavlink_frame_char_buffer(&receiveBuffer, &receiveStatus, static_cast<uint8_t>(byte), &message, &status) !=
                MAVLINK_FRAMING_OK ||
            message.msgid != MAVLINK_MSG_ID_COMMAND_LONG) {
            continue;
        }
        mavlink_command_long_t command{};
        mavlink_msg_command_long_decode(&message, &command);
        const int output = static_cast<int>(command.param1);
        const int pwm = static_cast<int>(command.param2);
        if (command.command != MAV_CMD_DO_SET_SERVO || output < 1 || output > 16 || pwm == _lostServoPwm) {
            continue;
        }
        if (pwm == _heldServoPwm) {
            _heldServoCommands.append({output, pwm});
        } else {
            _mockOutputs[output - 1] = pwm;
        }
    }
}

void ThrusterMappingIntegrationTest::_releaseHeldServoCommands()
{
    for (const auto& command : std::as_const(_heldServoCommands)) {
        _mockOutputs[command.first - 1] = command.second;
    }
    _heldServoCommands.clear();
}

void ThrusterMappingIntegrationTest::_reportMockOutputs(LinkInterface* link)
{
    if (!_outputsReported || !_mockLink || !_vehicle) {
        return;
    }
    if (_outputClockRuns) {
        _mockOutputTimeUsec += 20000;
    }
    _deliverOutputReport(link, _mockOutputTimeUsec, _mockOutputs);
    ++_mockOutputReportsSent;
}

void ThrusterMappingIntegrationTest::_deliverOutputReport(LinkInterface* link, quint32 timeUsec,
                                                          const std::array<int, 16>& outputs)
{
    // Also used on its own, to deliver a report that was made earlier than it arrives.
    mavlink_message_t report{};
    mavlink_msg_servo_output_raw_pack_chan(
        static_cast<uint8_t>(_vehicle->id()), MAV_COMP_ID_AUTOPILOT1, _mockLink->outgoingMavlinkChannel(), &report,
        timeUsec, 0, outputs[0], outputs[1], outputs[2], outputs[3], outputs[4], outputs[5], outputs[6], outputs[7],
        outputs[8], outputs[9], outputs[10], outputs[11], outputs[12], outputs[13], outputs[14], outputs[15]);
    MAVLinkProtocol::instance()->receiveBytes(link, encodedMessage(report));
}

void ThrusterMappingIntegrationTest::_filterMockResponse(LinkInterface* link, const QByteArray& bytes)
{
    _forwardMockResponse(link, bytes);
    // An output report follows every packet of the mock, so reports stop when the mock goes silent.
    _reportMockOutputs(link);
}

void ThrusterMappingIntegrationTest::_forwardMockResponse(LinkInterface* link, const QByteArray& bytes)
{
    mavlink_message_t receiveBuffer{};
    mavlink_status_t receiveStatus{};
    mavlink_message_t message{};
    mavlink_status_t status{};
    bool decoded = false;
    for (const char byte : bytes) {
        if (mavlink_frame_char_buffer(&receiveBuffer, &receiveStatus, static_cast<uint8_t>(byte), &message, &status) ==
            MAVLINK_FRAMING_OK) {
            decoded = true;
            break;
        }
    }
    if (decoded && message.msgid == MAVLINK_MSG_ID_AUTOPILOT_VERSION && _mockLink) {
        mavlink_autopilot_version_t version{};
        mavlink_msg_autopilot_version_decode(&message, &version);
        version.uid = _fixtureUid;
        mavlink_message_t rewritten{};
        mavlink_msg_autopilot_version_encode_chan(message.sysid, message.compid, _mockLink->outgoingMavlinkChannel(),
                                                  &rewritten, &version);
        MAVLinkProtocol::instance()->receiveBytes(link, encodedMessage(rewritten));
        return;
    }
    if (decoded && message.msgid == MAVLINK_MSG_ID_COMMAND_ACK && _mockLink) {
        mavlink_command_ack_t ack{};
        mavlink_msg_command_ack_decode(&message, &ack);
        if (_holdArmingUpdates && ack.command == MAV_CMD_COMPONENT_ARM_DISARM) {
            return;
        }
        if (ack.command == MAV_CMD_DO_SET_SERVO) {
            if (_servoAckPolicy == HoldServoAck) {
                return;
            }
            // Stock MockLink reports SET_SERVO unsupported; emulate acceptance
            // without bypassing Vehicle's pending-command and callback handling.
            ack.result = MAV_RESULT_ACCEPTED;
            mavlink_message_t rewritten{};
            mavlink_msg_command_ack_encode_chan(message.sysid, message.compid, _mockLink->outgoingMavlinkChannel(),
                                                &rewritten, &ack);
            MAVLinkProtocol::instance()->receiveBytes(link, encodedMessage(rewritten));
            return;
        }
    }
    if (decoded && (_holdArmingUpdates || _holdArmingHeartbeats) && message.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
        return;
    }
    MAVLinkProtocol::instance()->receiveBytes(link, bytes);
}

void ThrusterMappingIntegrationTest::cleanup()
{
    VehicleTestManualConnect::cleanup();
    QObject::disconnect(_responseFilter);
    QObject::disconnect(_commandWatch);
    _outputsReported = true;
    _outputClockRuns = true;
}

void ThrusterMappingIntegrationTest::_abortWaitsForPendingPwmAck()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy completed(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    _servoAckPolicy = HoldServoAck;
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 1000));
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 1, 2000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingTestAck);
    QVERIFY(_vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_DO_SET_SERVO));
    QVERIFY(!controller.beginFunctionSave(2, 34, 35));
    controller.abort(QStringLiteral("Stop while first PWM ACK is pending"));
    QCOMPARE(controller.state(), ThrusterDirectControlController::WaitingTestAck);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 1);
    _servoAckPolicy = AcceptServoAck;
    _mockLink->sendUnexpectedCommandAck(MAV_CMD_DO_SET_SERVO, MAV_RESULT_ACCEPTED);
    QTRY_VERIFY2_WITH_TIMEOUT(completed.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)), 3000);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 2);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    QVERIFY(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_REQUEST_READ) >= 2);
    QVERIFY(!_vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_DO_SET_SERVO));
    const auto commands = sentCommands(sent, MAV_CMD_DO_SET_SERVO);
    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.first().param2, 1550.0f);
    QCOMPARE(commands.last().param2, 1500.0f);
}

void ThrusterMappingIntegrationTest::_parameterRejectionCannotConfirmDisable()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Utilities.QGCStateMachine", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Timeout \".*WaitForParamResponseState\"")));
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("showAppMessage:.*Parameter (write|read) failed")));
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);
    QSignalSpy completed(&controller, &ThrusterDirectControlController::recoveryCompleted);
    connect(_vehicle->parameterManager(), &ParameterManager::_paramSetFailure, this, [this](int, const QString&) {
        _mockLink->setParamSetFailureMode(MockLink::FailParamSetNone);
        _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNoResponse);
    });
    _mockLink->setParamSetFailureMode(MockLink::FailParamSetParamError);
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 1000));
    QTRY_COMPARE_WITH_TIMEOUT(recoveryRequired.count(), 1, 4000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(completed.count(), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    // ParameterManager also refreshes a rejected SET independently. Keep the
    // owned link alive until that read has reached its failure result.
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 2000);
    _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNone);
}

void ThrusterMappingIntegrationTest::_missingNeutralAckKeepsQmlRecoveryJournal()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries:")));
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    QSignalSpy outputFunctionsRead(controller, &ThrusterDirectControlController::outputFunctionsRefreshed);
    traceController(controller, _mockLink);
    fixture.dialog->setProperty("testMode", 1);
    fixture.dialog->setProperty("testEnabled", true);
    QVERIFY(fixture.dialog->property("canStartDirectTest").toBool());
    _servoAckPolicy = HoldServoAck;
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(outputReportsArrive(fixture.controller()));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "startDirectServoTest", Q_ARG(QVariant, QVariant(1))));
    QTRY_VERIFY2_WITH_TIMEOUT(controller->state() == ThrusterDirectControlController::RecoveryNeeded,
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 15000);
    // One test PWM, then every neutral attempt.
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 5);
    QVERIFY(settings->property("recoveryPending").toBool());
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
    QCOMPARE(fixture.dialog->property("canPrepareMotorTest").toBool(), false);
    QVERIFY(!controller->beginFunctionSave(1, 0, 34));
    QVERIFY(!fixture.dialog->property("canConfigureServoFunctions").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "requestServoFunctionSave", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(34))));
    QVERIFY(!qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation")));
    // Confirmed disarming must release its pending flags even when the output
    // recovery journal keeps this dialog open.
    fixture.dialog->setProperty("disarmRequested", true);
    fixture.dialog->setProperty("disarmUnconfirmed", true);
    fixture.dialog->setProperty("disarmAckConfirmed", true);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "completeSafeShutdown"));
    QVERIFY(settings->property("recoveryPending").toBool());
    QVERIFY(!fixture.dialog->property("disarmRequested").toBool());
    QVERIFY(!fixture.dialog->property("disarmUnconfirmed").toBool());
    QObject* recover = dialogItem(fixture.dialog.get(), QStringLiteral("outputRecovery"));
    QVERIFY(recover);
    QVERIFY(recover->property("enabled").toBool());
    _servoAckPolicy = AcceptServoAck;
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QTRY_VERIFY2_WITH_TIMEOUT(controller->state() == ThrusterDirectControlController::Idle,
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 3000);
    QVERIFY(!settings->property("recoveryPending").toBool());
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    // After a recovery the tool reads the functions of all outputs again. Let that finish on this link.
    QTRY_COMPARE_WITH_TIMEOUT(outputFunctionsRead.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 5000);
}

void ThrusterMappingIntegrationTest::_linkLossAndReturnSendNeutralAgain()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries:")));
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 5000));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::Testing,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 3000);

    const auto neutralsSent = [&sent]() {
        int count = 0;
        for (const auto& command : sentCommands(sent, MAV_CMD_DO_SET_SERVO)) {
            if (command.param1 == 1.0f && command.param2 == 1500.0f) {
                ++count;
            }
        }
        return count;
    };
    QCOMPARE(neutralsSent(), 0);

    // The heartbeat stops while the test PWM is live: one neutral goes out in case only replies are lost.
    _mockLink->setCommLost(true);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    // The write reaches the spy through the link's thread.
    QTRY_COMPARE_WITH_TIMEOUT(neutralsSent(), 1, 5000);

    // The same link comes back: the output is still the one this session disabled, so neutral is sent again.
    _mockLink->setCommLost(false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(neutralsSent(), 2, 10000);

    // Only neutral is sent without the operator; writing the function back stays a manual step.
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
}

void ThrusterMappingIntegrationTest::_linkReturnLeavesAReassignedOutputAlone()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries:")));
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    QSignalSpy reissued(&controller, &ThrusterDirectControlController::neutralReissued);
    QSignalSpy skipped(&controller, &ThrusterDirectControlController::neutralReissueSkipped);
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 5000));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::Testing,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 3000);

    // A first loss and return, as in the previous test: neutral at the loss and again at the return.
    _mockLink->setCommLost(true);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    _mockLink->setCommLost(false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(reissued.count(), 2, 10000);

    // Someone gives the output another function while the session waits for a manual recovery.
    Fact* const function = controller.getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    // This write is not the tool's, so it raises the usual reboot prompt. The tool's own writes do not.
    expectAppMessage(rebootNotice());
    function->setRawValue(1);
    verifyExpectedLogMessage();
    QTRY_COMPARE_WITH_TIMEOUT(
        _mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 1, 5000);
    const qsizetype servoCommands = sentCommands(sent, MAV_CMD_DO_SET_SERVO).size();

    // No output of this session is live any more, so a later loss sends nothing on its own.
    _mockLink->setCommLost(true);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QCOMPARE(sentCommands(sent, MAV_CMD_DO_SET_SERVO).size(), servoCommands);

    // The return reads the function back, finds it is no longer Disabled and must not write a PWM over it.
    _mockLink->setCommLost(false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!skipped.isEmpty(), 10000);
    QCOMPARE(sentCommands(sent, MAV_CMD_DO_SET_SERVO).size(), servoCommands);
    QCOMPARE(reissued.count(), 2);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
}

void ThrusterMappingIntegrationTest::_linkLossLeavesALiveReassignedOutputAlone()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries:")));
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    QSignalSpy reissued(&controller, &ThrusterDirectControlController::neutralReissued);
    QSignalSpy skipped(&controller, &ThrusterDirectControlController::neutralReissueSkipped);
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 5000));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::Testing,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 3000);

    // While the test PWM is live the output is given another function, and the vehicle confirms it.
    Fact* const function = controller.getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    // This write is not the tool's, so it raises the usual reboot prompt. The tool's own writes do not.
    expectAppMessage(rebootNotice());
    function->setRawValue(1);
    verifyExpectedLogMessage();
    QTRY_COMPARE_WITH_TIMEOUT(
        _mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->parameterManager()->pendingWrites(), 5000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::Testing);
    const qsizetype servoCommands = sentCommands(sent, MAV_CMD_DO_SET_SERVO).size();

    // The heartbeat is lost. The output is known not to be Disabled any more, so nothing is written to it.
    _mockLink->setCommLost(true);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(skipped.count(), 1);
    QCOMPARE(reissued.count(), 0);
    _mockLink->setCommLost(false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(skipped.count(), 2, 10000);
    QCOMPARE(sentCommands(sent, MAV_CMD_DO_SET_SERVO).size(), servoCommands);
    QCOMPARE(reissued.count(), 0);
}

void ThrusterMappingIntegrationTest::_delayedReissueChecksTheOutputAgain()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries:")));
    // Connected before the controller binds, so that on a link return another command of the same kind is
    // already pending when the controller looks: its own neutral then has to wait.
    bool blockLinkReturn = false;
    const auto blocker = connect(_vehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this,
                                 [this, &blockLinkReturn](bool lost) {
                                     if (!lost && blockLinkReturn && _vehicle) {
                                         _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO,
                                                                  false, 2, 1500);
                                     }
                                 });
    const auto removeBlocker = qScopeGuard([blocker]() { QObject::disconnect(blocker); });
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    QSignalSpy skipped(&controller, &ThrusterDirectControlController::neutralReissueSkipped);
    const auto neutralsSent = [&sent]() {
        int count = 0;
        for (const auto& command : sentCommands(sent, MAV_CMD_DO_SET_SERVO)) {
            if (command.param1 == 1.0f && command.param2 == 1500.0f) {
                ++count;
            }
        }
        return count;
    };
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 5000));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::Testing,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 3000);

    _mockLink->setCommLost(true);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QTRY_COMPARE_WITH_TIMEOUT(neutralsSent(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_DO_SET_SERVO), 10000);

    // The link returns while another servo command is pending and unanswered, so the neutral cannot go out
    // at once. While it waits, the vehicle side gives the output another function. The ground cache still
    // says Disabled, so only asking the vehicle again can find out.
    _servoAckPolicy = HoldServoAck;
    blockLinkReturn = true;
    const int readsBeforeReturn = _mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_REQUEST_READ);
    _mockLink->setCommLost(false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->vehicleLinkManager()->communicationLost(), 10000);
    blockLinkReturn = false;
    QVERIFY(_vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_DO_SET_SERVO));
    mavlink_param_union_t reassigned{};
    reassigned.param_int16 = 1;
    _mockLink->setMockParamValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION"), reassigned.param_float);
    Q_UNUSED(readsBeforeReturn);

    QTRY_VERIFY_WITH_TIMEOUT(!skipped.isEmpty(), 15000);
    QCOMPARE(neutralsSent(), 1);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
}

void ThrusterMappingIntegrationTest::_lateTestReplyIsNotTakenForNeutral()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Giving up sending command after max retries:")));
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 4000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    QSignalSpy completed(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy pwmRequests(&controller, &ThrusterDirectControlController::servoPwmRequested);
    const auto neutralsSent = [&sent]() {
        int count = 0;
        for (const auto& command : sentCommands(sent, MAV_CMD_DO_SET_SERVO)) {
            if (command.param1 == 1.0f && command.param2 == 1500.0f) {
                ++count;
            }
        }
        return count;
    };
    // How many PWM commands the session had sent when it started to write the function back.
    int pwmRequestsAtRestore = -1;
    connect(&controller, &ThrusterDirectControlController::stateChanged, this,
            [&controller, &pwmRequests, &pwmRequestsAtRestore]() {
                if (controller.state() == ThrusterDirectControlController::WaitingRestore) {
                    pwmRequestsAtRestore = pwmRequests.count();
                }
            });

    // The test PWM reaches the output, but its reply does not arrive in time, so the session moves on to
    // neutral. That neutral is lost on the way: the output keeps the test PWM.
    _servoAckPolicy = HoldServoAck;
    _lostServoPwm = 1500;
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 5000));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::WaitingNeutralAck,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 8000);
    QTRY_COMPARE_WITH_TIMEOUT(neutralsSent(), 1, 5000);
    QVERIFY(_vehicle->isMavCommandPending(MAV_COMP_ID_AUTOPILOT1, MAV_CMD_DO_SET_SERVO));

    // Now the late reply to the test PWM comes in. The vehicle's queue hands it to the pending neutral,
    // because a reply names the command only. It must not be taken as "neutral confirmed": the output still
    // reports the test PWM, so the function stays disabled and neutral is sent again.
    _servoAckPolicy = AcceptServoAck;
    _mockLink->sendUnexpectedCommandAck(MAV_CMD_DO_SET_SERVO, MAV_RESULT_ACCEPTED);
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::ConfirmingNeutral,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 5000);
    QCOMPARE(_mockOutputs[0], 1550);
    // Reports from before the test, when the output was at neutral, arrive only now. They are older than
    // what the session has already taken, so they are not what it is waiting for.
    std::array<int, 16> outputsBeforeTheTest{};
    outputsBeforeTheTest.fill(1500);
    for (quint32 report = 0; report < 4; ++report) {
        _deliverOutputReport(_mockLink, _mockOutputTimeUsec - 60000000 + report * 500000, outputsBeforeTheTest);
    }
    QCOMPARE(controller.state(), ThrusterDirectControlController::ConfirmingNeutral);
    QCOMPARE(pwmRequestsAtRestore, -1);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
    // The second neutral arrives, and the vehicle answers it. Only then do the reports show neutral.
    _lostServoPwm = -1;
    QTRY_VERIFY2_WITH_TIMEOUT(completed.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)), 12000);
    QCOMPARE(pwmRequestsAtRestore, 3);
    QCOMPARE(neutralsSent(), 2);
    QCOMPARE(_mockOutputs[0], 1500);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
}

void ThrusterMappingIntegrationTest::_testNeedsOutputReports()
{
    // A vehicle that does not report its outputs: a test could be started but never be confirmed as ended.
    _outputsReported = false;
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(!controller.beginTest(1, 33, 1550, 1500, 1000));
    QCOMPARE(controller.state(), ThrusterDirectControlController::Idle);
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);

    // Reports whose time stands still cannot be put in order, so they cannot confirm anything either. The
    // test is refused before the function is touched, not left unconfirmed after it has run.
    _outputClockRuns = false;
    _outputsReported = true;
    const int reportsBefore = _mockOutputReportsSent;
    QTRY_VERIFY_WITH_TIMEOUT(_mockOutputReportsSent >= reportsBefore + 5, 5000);
    QVERIFY(!controller.outputReportsAvailable());
    QVERIFY(!controller.outputReportProblem().isEmpty());
    QVERIFY(!controller.beginTest(1, 33, 1550, 1500, 1000));
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);

    // Once usable reports arrive the same test starts, and ends on its own.
    _outputClockRuns = true;
    QVERIFY(outputReportsArrive(&controller));
    QSignalSpy completed(&controller, &ThrusterDirectControlController::recoveryCompleted);
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 1000));
    QTRY_VERIFY2_WITH_TIMEOUT(completed.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)), 8000);
    QCOMPARE(_mockOutputs[0], 1500);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
}

void ThrusterMappingIntegrationTest::_neutralWithoutOutputReportsKeepsTheOutput()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 500, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);
    QSignalSpy completed(&controller, &ThrusterDirectControlController::recoveryCompleted);
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 5000));
    QTRY_VERIFY2_WITH_TIMEOUT(controller.state() == ThrusterDirectControlController::Testing,
                              qPrintable(controllerDiagnostics(&controller, _mockLink)), 3000);

    // The reports stop. Every neutral is accepted, and none of them can be confirmed: the vehicle does not
    // say what the output holds. The function stays disabled and the output is kept for manual recovery.
    _outputsReported = false;
    controller.finishTest();
    QTRY_VERIFY2_WITH_TIMEOUT(recoveryRequired.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)),
                              10000);
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QCOMPARE(completed.count(), 0);
    // One test PWM, then every neutral attempt.
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 5);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
    QCOMPARE(controller.unconfirmedOutputForTest(), 1);

    // With reports back, a recovery confirms neutral and writes the function back.
    _outputsReported = true;
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginRecovery(1, 33, QStringLiteral("manual recovery"), 1500));
    QTRY_VERIFY2_WITH_TIMEOUT(completed.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)), 8000);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
}

void ThrusterMappingIntegrationTest::_testPwmStillOnItsWayIsNotClearedByNeutralReports()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ThrusterDirectControlController controller;
    QVERIFY(controller.bindVehicle(_vehicle));
    controller.setTimingsForTest(1, 2000, 1000);
    traceController(&controller, _mockLink);
    QSignalSpy recoveryRequired(&controller, &ThrusterDirectControlController::recoveryRequired);
    QSignalSpy completed(&controller, &ThrusterDirectControlController::recoveryCompleted);

    // The test PWM is held up on its way to the output, and a reply arrives all the same, as the late reply
    // of an earlier command would. The session takes the test for started and, when its time is up, sends
    // neutral, which is answered too. The vehicle reports the output at neutral, truthfully: it has not
    // left neutral yet.
    _heldServoPwm = 1550;
    QVERIFY(outputReportsArrive(&controller));
    QVERIFY(controller.beginTest(1, 33, 1550, 1500, 1000));
    QTRY_VERIFY2_WITH_TIMEOUT(recoveryRequired.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)),
                              10000);

    // Those reports do not show that the test PWM is over. Writing the function back and clearing the
    // record here would leave nothing to end it when it arrives.
    QCOMPARE(controller.state(), ThrusterDirectControlController::RecoveryNeeded);
    QVERIFY(recoveryRequired.constFirst().constFirst().toString().contains(QStringLiteral("没有上报过测试值")));
    QCOMPARE(completed.count(), 0);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
    QCOMPARE(controller.unconfirmedOutputForTest(), 1);
    QCOMPARE(_mockOutputs[0], 1500);

    // Now it arrives. The session is still open, and the operator's recovery ends it.
    _heldServoPwm = -1;
    _releaseHeldServoCommands();
    QCOMPARE(_mockOutputs[0], 1550);
    QVERIFY(controller.beginRecovery(1, 33, QStringLiteral("manual recovery"), 1500));
    QTRY_VERIFY2_WITH_TIMEOUT(completed.count() == 1, qPrintable(controllerDiagnostics(&controller, _mockLink)), 8000);
    QCOMPARE(_mockOutputs[0], 1500);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
}

void ThrusterMappingIntegrationTest::_qmlModesAndDisconnectedExport()
{
    _connectFixture(0x1010);
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    QVERIFY(fixture.dialog->property("vehicleReady").toBool());
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "prepareForNextTest"));
    QVERIFY(!_mockLink->armed());
    fixture.dialog->setProperty("testEnabled", true);
    fixture.dialog->setProperty("testMode", 1);
    QVERIFY(!fixture.dialog->property("canPrepareMotorTest").toBool());
    QVERIFY(fixture.dialog->property("canStartDirectTest").toBool());
    QVERIFY(!fixture.dialog->property("canConfigureServoFunctions").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "requestServoFunctionSave", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(34))));
    QVERIFY(!qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation")));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "prepareForNextTest"));
    QVERIFY(!_mockLink->armed());
    settings->setProperty("recoveryPending", true);
    fixture.dialog->setProperty("testMode", 0);
    QVERIFY(!fixture.dialog->property("canPrepareMotorTest").toBool());
    QVERIFY(!fixture.dialog->property("canStartMotorTest").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "prepareForNextTest"));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "testOutput", Q_ARG(QVariant, QVariant(1))));
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST), 0);
    settings->setProperty("recoveryPending", false);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "setPortName", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(QStringLiteral("Fixture MAIN1")))));
    const QString uid = _vehicle->vehicleUIDStr();
    QPointer<Vehicle> destroyedVehicle = _vehicle;
    _disconnectMockLink();
    QTRY_VERIFY_WITH_TIMEOUT(!destroyedVehicle, 3000);
    QCOMPARE(controller->vehicle(), nullptr);
    QVERIFY(!controller->parameterExists(-1, QStringLiteral("SERVO1_FUNCTION")));
    QCOMPARE(controller->getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION")), nullptr);
    const QString csv = csvText(fixture.dialog.get());
    QVERIFY(!csv.isEmpty());
    QVERIFY(csv.contains(uid));
    QVERIFY(csv.contains(QStringLiteral("Fixture MAIN1")));
    QVERIFY(!fixture.dialog->property("canStartDirectTest").toBool());
}

void ThrusterMappingIntegrationTest::_safeEndWaitsForPendingArmAndDisarms_data()
{
    QTest::addColumn<int>("armOutcome");
    QTest::newRow("close-before-arm-ack") << 0;
    QTest::newRow("close-after-arm-ack-before-heartbeat") << 1;
    QTest::newRow("no-arm-response-automatically-disarms") << 2;
}

void ThrusterMappingIntegrationTest::_safeEndWaitsForPendingArmAndDisarms()
{
    QFETCH(int, armOutcome);
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    fixture.dialog->setProperty("testMode", 0);
    fixture.dialog->setProperty("testEnabled", true);
    QVERIFY(fixture.dialog->property("canPrepareMotorTest").toBool());
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    QSignalSpy sent(_mockLink, &MockLink::writeBytesQueuedSignal);
    _holdArmingUpdates = true;
    _holdArmingHeartbeats = true;
    if (armOutcome == 2) {
        expectLogMessage("Vehicle.MavCommandQueue", QtWarningMsg,
                         QRegularExpression(QStringLiteral(
                             "Giving up sending command after max retries: MAV_CMD_COMPONENT_ARM_DISARM")));
        expectAppMessage(QRegularExpression(QRegularExpression::escape(
            QCoreApplication::translate("MavCommandQueue", "Vehicle did not respond to command: %1").arg(QString()))));
    }
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "prepareForNextTest"));
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1, 2000);
    QVERIFY(_mockLink->armed());
    QVERIFY(!_vehicle->armed());
    QVERIFY(fixture.dialog->property("armingRequested").toBool());
    QVERIFY(!fixture.dialog->property("canConfigureServoFunctions").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "requestServoFunctionSave", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(34))));
    QVERIFY(!qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation")));
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    if (armOutcome == 1) {
        _holdArmingUpdates = false;
        _mockLink->sendUnexpectedCommandAck(MAV_CMD_COMPONENT_ARM_DISARM, MAV_RESULT_ACCEPTED);
        QTRY_VERIFY_WITH_TIMEOUT(!fixture.dialog->property("armCommandPending").toBool(), 1000);
        QVERIFY(fixture.dialog->property("armingRequested").toBool());
        QVERIFY(!_vehicle->armed());
    }
    if (armOutcome != 2) {
        QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "beginSafeShutdown", Q_ARG(QVariant, QVariant(true)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("Close before armed heartbeat")))));
        // A stale disarmed heartbeat cannot close a session while ARM is in flight.
        QVERIFY(fixture.dialog->property("closeAfterSafeShutdown").toBool());
        if (armOutcome == 0) {
            QVERIFY(fixture.dialog->property("armingRequested").toBool());
            _holdArmingUpdates = false;
            _mockLink->sendUnexpectedCommandAck(MAV_CMD_COMPONENT_ARM_DISARM, MAV_RESULT_ACCEPTED);
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 2, 3000);
    if (armOutcome == 2) {
        verifyExpectedLogMessage();
        verifyExpectedLogMessage();
        QVERIFY(fixture.dialog->property("disarmRequested").toBool());
        QVERIFY(fixture.dialog->property("disarmUnconfirmed").toBool());
        fixture.dialog->setProperty("testEnabled", true);
        QVERIFY(!fixture.dialog->property("canPrepareMotorTest").toBool());
        QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "prepareForNextTest"));
        fixture.dialog->setProperty("testMode", 1);
        QVERIFY(!fixture.dialog->property("canStartDirectTest").toBool());
        QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "startDirectServoTest", Q_ARG(QVariant, QVariant(1))));
        QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
        QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 2);
        QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "beginSafeShutdown", Q_ARG(QVariant, QVariant(true)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("Wait for DISARM confirmation")))));
        QVERIFY(fixture.dialog->property("closeAfterSafeShutdown").toBool());
        _holdArmingUpdates = false;
        _mockLink->sendUnexpectedCommandAck(MAV_CMD_COMPONENT_ARM_DISARM, MAV_RESULT_ACCEPTED);
    }
    _holdArmingHeartbeats = false;
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.dialog->property("closeAfterSafeShutdown").toBool(), 3000);
    QVERIFY(!_mockLink->armed());
    QVERIFY(!_vehicle->armed());
    QVERIFY(!fixture.dialog->property("armingRequested").toBool());
    QVERIFY(!fixture.dialog->property("disarmRequested").toBool());
    const auto commands = sentCommands(sent, MAV_CMD_COMPONENT_ARM_DISARM);
    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.first().param1, 1.0f);
    QCOMPARE(commands.last().param1, 0.0f);
}

void ThrusterMappingIntegrationTest::_freshBackupOverridesStaleCache()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    Fact* fact = controller->getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(fact);
    fact->containerSetRawValue(QVariant::fromValue<qint16>(77));
    QCOMPARE(fact->rawValue().toInt(), 77);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    controller->setTimingsForTest(1, 2000, 1000);
    traceController(controller, _mockLink);
    fixture.dialog->setProperty("testMode", 1);
    fixture.dialog->setProperty("testEnabled", true);
    _servoAckPolicy = HoldServoAck;
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(outputReportsArrive(fixture.controller()));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "startDirectServoTest", Q_ARG(QVariant, QVariant(1))));
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 1, 3000);
    QVERIFY(settings->property("recoveryPending").toBool());
    QCOMPARE(settings->property("recoveryFunction").toInt(), 33);
    QCOMPARE(controller->originalFunction(), 33);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "beginSafeShutdown", Q_ARG(QVariant, QVariant(false)),
                                      Q_ARG(QVariant, QVariant(QStringLiteral("Finish stale cache regression")))));
    _servoAckPolicy = AcceptServoAck;
    _mockLink->sendUnexpectedCommandAck(MAV_CMD_DO_SET_SERVO, MAV_RESULT_ACCEPTED);
    QTRY_VERIFY2_WITH_TIMEOUT(controller->state() == ThrusterDirectControlController::Idle,
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 3000);
    QVERIFY(!settings->property("recoveryPending").toBool());
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
}

void ThrusterMappingIntegrationTest::_backupReadFailureDoesNotCreateRecoveryJournal()
{
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    ignoreLogMessage("Utilities.QGCStateMachine", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Timeout \".*WaitForParamResponseState\"")));
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("showAppMessage:.*Parameter read failed")));
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    fixture.dialog->setProperty("testMode", 1);
    fixture.dialog->setProperty("testEnabled", true);
    _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNoResponse);
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    QSignalSpy failedRead(_vehicle->parameterManager(), &ParameterManager::_paramRequestReadFailure);
    QVERIFY(outputReportsArrive(fixture.controller()));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "startDirectServoTest", Q_ARG(QVariant, QVariant(1))));
    QTRY_VERIFY_WITH_TIMEOUT(!failedRead.isEmpty(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(controller->state(), ThrusterDirectControlController::Idle, 3000);
    QVERIFY(!settings->property("recoveryPending").toBool());
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNone);
}

void ThrusterMappingIntegrationTest::_unfinishedRecoveryIsAnnouncedOnConnect()
{
    // A run that died mid-test leaves only this record behind; the tool is not open after a restart.
    {
        QSettings journal;
        journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        journal.setValue(QStringLiteral("recoveryPending"), true);
        journal.setValue(QStringLiteral("recoveryOutput"), 3);
        journal.setValue(QStringLiteral("recoveryFunction"), 35);
        journal.setValue(QStringLiteral("recoveryTimestamp"), QStringLiteral("2026-10-11 09:00"));
    }
    // An armed vehicle that skips the parameter download never becomes parameter-ready, and neither
    // does one whose first parameter request fails. The record is local, so the notice must not wait.
    auto* const skipSetting = SettingsManager::instance()->mavlinkSettings()->noInitialDownloadWhenFlying();
    const QVariant previousSkip = skipSetting->rawValue();
    const auto restoreSkip = qScopeGuard([&]() { skipSetting->setRawValue(previousSkip); });
    skipSetting->setRawValue(true);
    expectAppMessage(QRegularExpression(QStringLiteral("SERVO3")));
    LinkManager::instance()->setConnectionsAllowed();
    auto* const config = new MockConfiguration(QStringLiteral("ArmedConnectRecoveryNotice"));
    config->setFirmwareType(MAV_AUTOPILOT_ARDUPILOTMEGA);
    config->setVehicleType(MAV_TYPE_SUBMARINE);
    config->setStartArmed(true);
    config->setDynamic(true);
    auto linkConfig = LinkManager::instance()->addConfiguration(config);
    QVERIFY(LinkManager::instance()->createConnectedLink(linkConfig));
    _mockLink = qobject_cast<MockLink*>(linkConfig->link());
    QVERIFY(_mockLink);
    connect(_mockLink, &QObject::destroyed, this, [this]() { _mockLink = nullptr; });
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->isInitialConnectComplete(), 15000);
    verifyExpectedLogMessage();
    QVERIFY(!_vehicle->parameterManager()->parametersReady());

    {
        QSettings journal;
        journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        journal.setValue(QStringLiteral("recoveryPending"), false);
    }
    QCOMPARE(ThrusterDirectControlController::pendingRecoveryNotice(), QString());
}

void ThrusterMappingIntegrationTest::_recoveryJournalIsStoredBeforeDisable()
{
    // A separate QSettings sees only what the tool has handed to the store, not what still sits in the
    // Settings property cache. This checks the order of the hand-over, not that the file reached the disk.
    const auto storedJournal = [] {
        QSettings store;
        store.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        return QVariantList{store.value(QStringLiteral("recoveryPending"), false).toBool(),
                            store.value(QStringLiteral("recoveryOutput"), -1).toInt(),
                            store.value(QStringLiteral("recoveryFunction"), -1).toInt()};
    };
    QVariantList atDisable;
    QVariantList afterRecovery;

    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    QVERIFY(controller);
    controller->setTimingsForTest(1, 2000, 1000);
    fixture.dialog->setProperty("testMode", 1);
    fixture.dialog->setProperty("testEnabled", true);
    QVERIFY(fixture.dialog->property("canStartDirectTest").toBool());
    connect(controller, &ThrusterDirectControlController::functionWriteRequested, this,
            [&atDisable, storedJournal](int value) {
                if (value == 0 && atDisable.isEmpty()) {
                    atDisable = storedJournal();
                }
            });
    // The tool's own handler is connected first, so it has already cleared the record here.
    connect(controller, &ThrusterDirectControlController::recoveryCompleted, this,
            [&afterRecovery, storedJournal]() { afterRecovery = storedJournal(); });

    QVERIFY(outputReportsArrive(fixture.controller()));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "startDirectServoTest", Q_ARG(QVariant, QVariant(1))));
    QTRY_VERIFY2_WITH_TIMEOUT(!afterRecovery.isEmpty(), qPrintable(controllerDiagnostics(controller, _mockLink)), 15000);
    QCOMPARE(atDisable, (QVariantList{true, 1, 33}));
    QCOMPARE(afterRecovery.first().toBool(), false);
}

void ThrusterMappingIntegrationTest::_recordWithoutSerialNumberNeedsManualConfirmation()
{
    auto* const allowManual = SettingsManager::instance()->appSettings()->allowManualOutputRecovery();
    const QVariant previousAllow = allowManual->rawValue();
    const auto restoreAllow = qScopeGuard([&]() { allowManual->setRawValue(previousAllow); });
    allowManual->setRawValue(true);

    // The flight controller reports no serial number, and an earlier run left SERVO1 disabled.
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    QCOMPARE(_vehicle->vehicleUID(), quint64(0));
    Fact* const function = _vehicle->parameterManager()->getParameter(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    expectAppMessage(rebootNotice());
    function->setRawValue(0);
    QTRY_COMPARE_WITH_TIMEOUT(
        _mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0, 5000);
    verifyExpectedLogMessage();
    {
        QSettings journal;
        journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        journal.setValue(QStringLiteral("recoveryPending"), true);
        journal.setValue(QStringLiteral("recoveryVehicleId"), _vehicle->id());
        journal.setValue(QStringLiteral("recoveryVehicleUid"), QString());
        journal.setValue(QStringLiteral("recoveryOutput"), 1);
        journal.setValue(QStringLiteral("recoveryFunction"), 33);
        journal.setValue(QStringLiteral("recoveryTimestamp"), QStringLiteral("2026-10-11 09:00"));
        journal.setValue(QStringLiteral("recoveryNeutralPwm"), 1500);
        journal.setValue(QStringLiteral("recoveryOperation"), QStringLiteral("thruster"));
    }

    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    QSignalSpy outputFunctionsRead(controller, &ThrusterDirectControlController::outputFunctionsRefreshed);
    QVERIFY(settings->property("recoveryPending").toBool());
    QObject* recover = dialogItem(fixture.dialog.get(), QStringLiteral("outputRecovery"));
    QVERIFY(recover);

    // Nothing can prove that the record belongs to this vehicle, so the tool refuses on its own...
    QVERIFY(!recover->property("enabled").toBool());

    // ...and with the setting off there is no way around that.
    allowManual->setRawValue(false);
    QVERIFY(!fixture.dialog->property("recoveryManualConfirmAvailable").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(!recover->property("enabled").toBool());

    // With it on, the operator has to confirm twice before the recovery is offered.
    allowManual->setRawValue(true);
    QVERIFY(fixture.dialog->property("recoveryManualConfirmAvailable").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(!recover->property("enabled").toBool());
    QVERIFY(fixture.dialog->property("directServoStatusText").toString().contains(QStringLiteral("2026-10-11 09:00")));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(recover->property("enabled").toBool());

    // Switching the setting off afterwards withdraws the permission, not only the offer. The operator's
    // statement that the record belongs to this vehicle stands, so the record cannot be discarded either.
    allowManual->setRawValue(false);
    QVERIFY(!recover->property("enabled").toBool());
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QCOMPARE(controller->state(), ThrusterDirectControlController::Idle);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
    QVERIFY(settings->property("recoveryPending").toBool());
    QVariant discardable;
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoveryJournalCanBeDiscarded",
                                      Q_RETURN_ARG(QVariant, discardable)));
    QVERIFY(!discardable.toBool());
    allowManual->setRawValue(true);
    QVERIFY(recover->property("enabled").toBool());

    // A change of the active vehicle withdraws the confirmation itself: it was given for one session with
    // one vehicle in front of the operator.
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "revokeManualRecoveryConfirmation"));
    QVERIFY(!recover->property("enabled").toBool());
    QVERIFY(fixture.dialog->property("recoveryManualConfirmAvailable").toBool());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(recover->property("enabled").toBool());

    // The recovery itself is the normal one: neutral first, then the original function.
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QTRY_VERIFY2_WITH_TIMEOUT(controller->state() == ThrusterDirectControlController::Idle,
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 5000);
    // A recovery knows nothing about earlier PWM commands. One neutral is enough all the same, because it is
    // the vehicle's report of the output that confirms it, not the reply.
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 1);
    QCOMPARE(_mockOutputs[0], 1500);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    QVERIFY(!settings->property("recoveryPending").toBool());
    // After a recovery the tool reads the functions of all outputs again. Let that finish on this link.
    QTRY_COMPARE_WITH_TIMEOUT(outputFunctionsRead.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 5000);
}

void ThrusterMappingIntegrationTest::_recoveryOfAnOutputThatKeepsTakingPwmAsksTheOperator()
{
    auto* const allowManual = SettingsManager::instance()->appSettings()->allowManualOutputRecovery();
    const QVariant previousAllow = allowManual->rawValue();
    const auto restoreAllow = qScopeGuard([&]() { allowManual->setRawValue(previousAllow); });
    allowManual->setRawValue(true);

    // An earlier run left a record for SERVO1, whose original function was "disabled": with that function
    // written back, the flight controller still takes a direct PWM command for it.
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    Fact* const function = _vehicle->parameterManager()->getParameter(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    expectAppMessage(rebootNotice());
    function->setRawValue(0);
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(),
                              0, 5000);
    verifyExpectedLogMessage();
    {
        QSettings journal;
        journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        journal.setValue(QStringLiteral("recoveryPending"), true);
        journal.setValue(QStringLiteral("recoveryVehicleId"), _vehicle->id());
        journal.setValue(QStringLiteral("recoveryVehicleUid"), QString());
        journal.setValue(QStringLiteral("recoveryOutput"), 1);
        journal.setValue(QStringLiteral("recoveryFunction"), 0);
        journal.setValue(QStringLiteral("recoveryTimestamp"), QStringLiteral("2026-10-12 09:00"));
        journal.setValue(QStringLiteral("recoveryNeutralPwm"), 1500);
        journal.setValue(QStringLiteral("recoveryOperation"), QStringLiteral("thruster"));
    }
    _mockOutputs[0] = 1550;

    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    QSignalSpy outputFunctionsRead(controller, &ThrusterDirectControlController::outputFunctionsRefreshed);
    traceController(controller, _mockLink);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QObject* recover = dialogItem(fixture.dialog.get(), QStringLiteral("outputRecovery"));
    QVERIFY(recover);
    QVERIFY(recover->property("enabled").toBool());
    QVERIFY(outputReportsArrive(controller));

    // The first request sends nothing. It says what the software cannot rule out and asks for the operator's
    // word that the device on this output has stopped.
    _mockLink->clearReceivedMavCommandCounts();
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QCOMPARE(controller->state(), ThrusterDirectControlController::Idle);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QVERIFY(fixture.dialog->property("recoveryOperatorConfirmation").toBool());
    QVERIFY(
        fixture.dialog->property("directServoStatusText").toString().contains(QStringLiteral("不在软件能自动恢复的范围内")));
    QVERIFY(recover->property("text").toString().contains(QStringLiteral("确认设备已停止")));
    QVERIFY(settings->property("recoveryPending").toBool());

    // The second one is that word: neutral, the reports of the output, then the record is cleared.
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QTRY_VERIFY2_WITH_TIMEOUT(!settings->property("recoveryPending").toBool(),
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 8000);
    QCOMPARE(controller->state(), ThrusterDirectControlController::Idle);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 1);
    QCOMPARE(_mockOutputs[0], 1500);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 0);
    QVERIFY(!fixture.dialog->property("recoveryOperatorConfirmation").toBool());
    // What a recovery has confirmed is this one output, and the tool says so once it has read the others back.
    QTRY_VERIFY_WITH_TIMEOUT(
        fixture.dialog->property("outputMappingNotice").toString().contains(QStringLiteral("这只说明这一路")), 10000);
    // After a recovery the tool reads the functions of all outputs again. Let that finish on this link.
    QTRY_COMPARE_WITH_TIMEOUT(outputFunctionsRead.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 5000);
}

void ThrusterMappingIntegrationTest::_restoredMotorFunctionOnAnotherOutputIsReported()
{
    auto* const allowManual = SettingsManager::instance()->appSettings()->allowManualOutputRecovery();
    const QVariant previousAllow = allowManual->rawValue();
    const auto restoreAllow = qScopeGuard([&]() { allowManual->setRawValue(previousAllow); });
    allowManual->setRawValue(true);

    // An earlier run left SERVO1, a motor, disabled, and the flight controller was restarted in that state.
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    Fact* const function = _vehicle->parameterManager()->getParameter(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    expectAppMessage(rebootNotice());
    function->setRawValue(0);
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(),
                              0, 5000);
    verifyExpectedLogMessage();
    {
        QSettings journal;
        journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        journal.setValue(QStringLiteral("recoveryPending"), true);
        journal.setValue(QStringLiteral("recoveryVehicleId"), _vehicle->id());
        journal.setValue(QStringLiteral("recoveryVehicleUid"), QString());
        journal.setValue(QStringLiteral("recoveryOutput"), 1);
        journal.setValue(QStringLiteral("recoveryFunction"), 33);
        journal.setValue(QStringLiteral("recoveryTimestamp"), QStringLiteral("2026-10-12 20:00"));
        journal.setValue(QStringLiteral("recoveryNeutralPwm"), 1500);
        journal.setValue(QStringLiteral("recoveryOperation"), QStringLiteral("thruster"));
    }
    // A flight controller that starts while a motor function has no output gives that function to a default
    // output, in memory only. Which output that is does not matter to the tool; here the same function is
    // simply put on SERVO2 behind the ground station's back, which read SERVO2's function before.
    QVERIFY(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO2_FUNCTION")).toInt() != 33);
    mavlink_param_union_t filledIn{};
    filledIn.param_int16 = 33;
    _mockLink->setMockParamValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO2_FUNCTION"), filledIn.param_float);

    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    QSignalSpy outputFunctionsRead(controller, &ThrusterDirectControlController::outputFunctionsRefreshed);
    traceController(controller, _mockLink);
    QVERIFY(fixture.dialog->property("outputMappingNotice").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(outputReportsArrive(controller));

    // A motor output is recovered without the operator's word about the device.
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QVERIFY(!fixture.dialog->property("recoveryOperatorConfirmation").toBool());
    QTRY_VERIFY2_WITH_TIMEOUT(!settings->property("recoveryPending").toBool(),
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 8000);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);

    // SERVO1 is back and read back, and the record is cleared. The tool asks for the other outputs again
    // and reports that two of them now carry the same motor.
    QTRY_VERIFY_WITH_TIMEOUT(
        fixture.dialog->property("outputMappingNotice").toString().contains(QStringLiteral("SERVO1 与 SERVO2")), 8000);
    QVERIFY(fixture.dialog->property("outputMappingNotice").toString().contains(QStringLiteral("重启飞控")));
    // It is not replaced by the end of the safe shutdown, and the tool does not close over it.
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "completeSafeShutdown"));
    QVERIFY(fixture.dialog->property("outputMappingNotice").toString().contains(QStringLiteral("SERVO1 与 SERVO2")));
    QVERIFY(!fixture.dialog->property("closeAfterSafeShutdown").toBool());
    QObject* notice = dialogItem(fixture.dialog.get(), QStringLiteral("outputMappingNotice"));
    QVERIFY(notice);
    QVERIFY(notice->property("text").toString().contains(QStringLiteral("SERVO1 与 SERVO2")));
    // After a recovery the tool reads the functions of all outputs again. Let that finish on this link.
    QTRY_COMPARE_WITH_TIMEOUT(outputFunctionsRead.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 5000);
}

void ThrusterMappingIntegrationTest::_outputFunctionsThatWereNotReadBackAreNotReportedAsChecked()
{
    auto* const allowManual = SettingsManager::instance()->appSettings()->allowManualOutputRecovery();
    const QVariant previousAllow = allowManual->rawValue();
    const auto restoreAllow = qScopeGuard([&]() { allowManual->setRawValue(previousAllow); });
    allowManual->setRawValue(true);
    ignoreLogMessage("Utilities.QGCStateMachine", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Timeout \".*WaitForParamResponseState\"")));
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("showAppMessage:.*Parameter read failed")));

    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    Fact* const function = _vehicle->parameterManager()->getParameter(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    expectAppMessage(rebootNotice());
    function->setRawValue(0);
    QTRY_COMPARE_WITH_TIMEOUT(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(),
                              0, 5000);
    verifyExpectedLogMessage();
    {
        QSettings journal;
        journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
        journal.setValue(QStringLiteral("recoveryPending"), true);
        journal.setValue(QStringLiteral("recoveryVehicleId"), _vehicle->id());
        journal.setValue(QStringLiteral("recoveryVehicleUid"), QString());
        journal.setValue(QStringLiteral("recoveryOutput"), 1);
        journal.setValue(QStringLiteral("recoveryFunction"), 33);
        journal.setValue(QStringLiteral("recoveryTimestamp"), QStringLiteral("2026-10-12 20:00"));
        journal.setValue(QStringLiteral("recoveryNeutralPwm"), 1500);
        journal.setValue(QStringLiteral("recoveryOperation"), QStringLiteral("thruster"));
    }

    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    QSignalSpy outputFunctionsRead(controller, &ThrusterDirectControlController::outputFunctionsRefreshed);
    traceController(controller, _mockLink);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "confirmRecoveryVehicleManually"));
    QVERIFY(outputReportsArrive(controller));

    // The recovery works, but one of the reads of the other outputs' functions that follow it gets no
    // answer. The operator had asked for the tool to close when it is done.
    connect(controller, &ThrusterDirectControlController::recoveryCompleted, this,
            [this]() { _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNoResponse); });
    connect(_vehicle->parameterManager(), &ParameterManager::_paramRequestReadFailure, this,
            [this](int, const QString&, int) {
                _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNone);
            });
    fixture.dialog->setProperty("closeAfterSafeShutdown", true);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "recoverPendingServoFunction"));
    QTRY_VERIFY2_WITH_TIMEOUT(!settings->property("recoveryPending").toBool(),
                              qPrintable(controllerDiagnostics(controller, _mockLink)), 8000);

    // Until the answers are in, the tool says that the other outputs have not been checked, and it stays.
    QVERIFY(fixture.dialog->property("outputMappingNotice").toString().contains(QStringLiteral("尚未核对")));
    QVERIFY(!fixture.dialog->property("closeAfterSafeShutdown").toBool());
    // With one of them missing it does not say that no other output carries the motor.
    QTRY_COMPARE_WITH_TIMEOUT(outputFunctionsRead.count(), 1, 30000);
    QCOMPARE(outputFunctionsRead.constFirst().constFirst().toBool(), false);
    const QString notice = fixture.dialog->property("outputMappingNotice").toString();
    QVERIFY(notice.contains(QStringLiteral("没有核对它们")));
    QVERIFY(!notice.contains(QStringLiteral("没有相同的电机功能")));
    _mockLink->setParamRequestReadFailureMode(MockLink::FailParamRequestReadNone);
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 5000);
}

void ThrusterMappingIntegrationTest::_wiringRecordsAreScopedToVehicleUid()
{
    const auto recordPort = [](QObject* dialog, const QString& text) {
        return QMetaObject::invokeMethod(dialog, "setPortName", Q_ARG(QVariant, QVariant(1)),
                                         Q_ARG(QVariant, QVariant(text)));
    };
    _connectFixture(0x1111);
    QVERIFY(_vehicle && _mockLink);
    const QString uidA = _vehicle->vehicleUIDStr();
    {
        MappingDialogFixture fixture;
        QVERIFY2(fixture.dialog, qPrintable(fixture.error));
        QVERIFY(recordPort(fixture.dialog.get(), QStringLiteral("Vehicle A MAIN1")));
        QCOMPARE(portName(fixture.dialog.get(), 1), QStringLiteral("Vehicle A MAIN1"));
        _disconnectMockLink();
    }
    _connectFixture(0x2222);
    QVERIFY(_vehicle && _mockLink);
    const QString uidB = _vehicle->vehicleUIDStr();
    QVERIFY(uidA != uidB);
    {
        MappingDialogFixture fixture;
        QVERIFY2(fixture.dialog, qPrintable(fixture.error));
        QCOMPARE(portName(fixture.dialog.get(), 1), QString());
        QVERIFY(recordPort(fixture.dialog.get(), QStringLiteral("Vehicle B J3")));
        const auto records =
            QJsonDocument::fromJson(fixture.settings()->property("vehicleMappings").toString().toUtf8());
        QVERIFY(records.isObject());
        QVERIFY(records.object().contains(uidA));
        QVERIFY(records.object().contains(uidB));
        _disconnectMockLink();
    }
    _connectFixture(0x1111);
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QCOMPARE(portName(fixture.dialog.get(), 1), QStringLiteral("Vehicle A MAIN1"));
}

void ThrusterMappingIntegrationTest::_unknownUidRecordsStayInSession()
{
    QSettings settings;
    settings.setValue(QStringLiteral("DeepSharkServoOutputMapping/port1Name"), QStringLiteral("Legacy MAIN1"));
    settings.sync();
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    {
        MappingDialogFixture fixture;
        QVERIFY2(fixture.dialog, qPrintable(fixture.error));
        QCOMPARE(portName(fixture.dialog.get(), 1), QString());
        QVERIFY(fixture.dialog->property("legacyMappingsAvailable").toBool());
        QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "importLegacyMappings"));
        QCOMPARE(portName(fixture.dialog.get(), 1), QString());
        QVERIFY(fixture.dialog->property("legacyImportConfirmation").toBool());
        QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "importLegacyMappings"));
        QCOMPARE(portName(fixture.dialog.get(), 1), QStringLiteral("Legacy MAIN1"));
        QCOMPARE(fixture.settings()->property("vehicleMappings").toString(), QStringLiteral("{}"));
        QVERIFY(csvText(fixture.dialog.get()).contains(QStringLiteral("session only")));
        _disconnectMockLink();
    }
    _connectFixture();
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QCOMPARE(portName(fixture.dialog.get(), 1), QString());
    QCOMPARE(fixture.settings()->property("vehicleMappings").toString(), QStringLiteral("{}"));
}

void ThrusterMappingIntegrationTest::_functionSaveReadsBackAndRejectsStaleValues_data()
{
    QTest::addColumn<int>("outcome");
    QTest::newRow("confirmed-set-and-new-read") << 0;
    QTest::newRow("actual-original-changed-since-confirmation") << 1;
    QTest::newRow("firmware-rejects-function-write") << 2;
}

void ThrusterMappingIntegrationTest::_functionSaveReadsBackAndRejectsStaleValues()
{
    QFETCH(int, outcome);
    _connectFixture(0x4141);
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    auto* settings = fixture.settings();
    QVERIFY(controller && settings);
    controller->setTimingsForTest(1, 2000, 1000);
    traceController(controller, _mockLink);
    Fact* function = controller->getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    QCOMPARE(function->rawValue().toInt(), 33);
    settings->setProperty("recoveryOutput", 8);
    settings->setProperty("recoveryFunction", 72);
    settings->setProperty("recoveryTimestamp", QStringLiteral("Unrelated prior journal metadata"));
    settings->setProperty("recoveryNeutralPwm", 1510);
    settings->setProperty("recoveryOperation", QStringLiteral("servo"));
    QVERIFY(!settings->property("recoveryPending").toBool());
    QSignalSpy finished(controller, &ThrusterDirectControlController::functionSaveFinished);
    QSignalSpy backup(controller, &ThrusterDirectControlController::originalFunctionConfirmed);
    QSignalSpy restored(controller, &ThrusterDirectControlController::recoveryCompleted);
    QSignalSpy recovery(controller, &ThrusterDirectControlController::recoveryRequired);
    fixture.window->show();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.window->isExposed(), 3000);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "open"));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.dialog->property("visible").toBool(), 2000);
    QObject* choice = dialogItem(fixture.dialog.get(), QStringLiteral("outputFunctionChoice1"));
    QObject* save = dialogItem(fixture.dialog.get(), QStringLiteral("outputFunctionSave1"));
    QVERIFY(choice && save);
    QVERIFY(fixture.dialog->property("canConfigureServoFunctions").toBool());
    const auto enumValues = function->enumValues();
    int targetIndex = -1;
    for (int index = 0; index < enumValues.size(); ++index) {
        if (enumValues.at(index).toInt() == 34) {
            targetIndex = index;
            break;
        }
    }
    QVERIFY(targetIndex >= 0);
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    QVERIFY(choice->setProperty("currentIndex", targetIndex));
    QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, targetIndex)));
    QCOMPARE(function->rawValue().toInt(), 33);
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    QVERIFY(save->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
    QPointer<QObject> confirmation = qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation"));
    QVERIFY(confirmation);
    QCOMPARE(confirmation->objectName(), QStringLiteral("outputFunctionSaveConfirmation"));
    QVERIFY(confirmation->property("visible").toBool());
    QCOMPARE(confirmation->property("originalValue").toInt(), 33);
    QCOMPARE(confirmation->property("targetValue").toInt(), 34);
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    if (outcome == 0) {
        QSignalSpy frames(fixture.window.get(), &QQuickWindow::frameSwapped);
        fixture.window->requestUpdate();
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        const QImage image = fixture.window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir::current().filePath(QStringLiteral("output-tool-function-confirmation.png"))));
        QObject* cancel = dialogItem(confirmation, QStringLiteral("popupDialog_rejectButton"));
        QVERIFY(cancel && cancel->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation")), 2000);
        QVERIFY(freshMockTraffic(_mockLink, _vehicle));
        QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
        QCOMPARE(function->rawValue().toInt(), 33);
        QCOMPARE(finished.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        confirmation = qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation"));
        QVERIFY(confirmation && confirmation->property("visible").toBool());
    }
    if (outcome == 1) {
        // The setter intentionally takes bytewise MAVLink bits, while APM wire
        // PARAM_SET/READ uses numeric floats. Leave the ground cache at 33.
        mavlink_param_union_t changed{};
        changed.param_int16 = 35;
        _mockLink->setMockParamValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION"), changed.param_float);
        QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 35);
        QCOMPARE(function->rawValue().toInt(), 33);
    } else {
        expectAppMessage(rebootNotice());
        if (outcome == 2) {
            _mockLink->setParamSetFailureMode(MockLink::FailParamSetParamError);
            expectAppMessage(QRegularExpression(QStringLiteral("Parameter write failed: param: SERVO1_FUNCTION")));
        }
    }
    QObject* accept = dialogItem(confirmation, QStringLiteral("popupDialog_acceptButton"));
    QVERIFY(accept && accept->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
    QVERIFY(controller->functionSaveBusy());
    QVERIFY(!controller->beginTest(1, 33, 1550, 1500, 1000));
    QTRY_VERIFY2_WITH_TIMEOUT(finished.count() == 1, qPrintable(controllerDiagnostics(controller, _mockLink)), 3000);
    if (outcome != 1) {
        verifyExpectedLogMessage();
        if (outcome == 2) {
            verifyExpectedLogMessage();
        }
    }
    QCOMPARE(finished.first().at(0).toInt(), 1);
    QCOMPARE(finished.first().at(1).toBool(), outcome == 0);
    QVERIFY(!finished.first().at(2).toString().isEmpty());
    QCOMPARE(controller->state(), ThrusterDirectControlController::Idle);
    QVERIFY(!controller->functionSaveBusy());
    QTRY_VERIFY_WITH_TIMEOUT(parameterWorkFinished(_vehicle->parameterManager()), 2000);
    const int expected = outcome == 0 ? 34 : (outcome == 1 ? 35 : 33);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), expected);
    // After a rejected write the parameter manager reads the value back in a state machine of its own,
    // which may not have started yet when the one above has finished. Until then the ground value is
    // still the optimistic one.
    QTRY_COMPARE_WITH_TIMEOUT(function->rawValue().toInt(), expected, 5000);
    if (outcome == 0) {
        QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 1);
        QVERIFY(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_REQUEST_READ) >= 2);
    } else if (outcome == 1) {
        QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    }
    if (outcome == 0) {
        // An explicit same-value selection supports checking an uncertain prior
        // write again. It must fresh-read the hardware without another SET.
        QVERIFY(choice->setProperty("currentIndex", targetIndex));
        QVERIFY(QMetaObject::invokeMethod(choice, "activated", Q_ARG(int, targetIndex)));
        QTRY_VERIFY_WITH_TIMEOUT(save->property("enabled").toBool(), 1000);
        const int readsBefore = _mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_REQUEST_READ);
        QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        confirmation = qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation"));
        QVERIFY(confirmation && confirmation->property("visible").toBool());
        QCOMPARE(confirmation->property("originalValue").toInt(), 34);
        QCOMPARE(confirmation->property("targetValue").toInt(), 34);
        accept = dialogItem(confirmation, QStringLiteral("popupDialog_acceptButton"));
        QVERIFY(accept && accept->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        QTRY_VERIFY2_WITH_TIMEOUT(finished.count() == 2, qPrintable(controllerDiagnostics(controller, _mockLink)),
                                  3000);
        QVERIFY(finished.last().at(1).toBool());
        QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 1);
        QVERIFY(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_REQUEST_READ) > readsBefore);
        QCOMPARE(function->rawValue().toInt(), 34);
        QCOMPARE(controller->state(), ThrusterDirectControlController::Idle);
    }
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QCOMPARE(backup.count(), 0);
    QCOMPARE(restored.count(), 0);
    QCOMPARE(recovery.count(), 0);
    QVERIFY(!settings->property("recoveryPending").toBool());
    QCOMPARE(settings->property("recoveryOutput").toInt(), 8);
    QCOMPARE(settings->property("recoveryFunction").toInt(), 72);
    QCOMPARE(settings->property("recoveryTimestamp").toString(), QStringLiteral("Unrelated prior journal metadata"));
    QCOMPARE(settings->property("recoveryNeutralPwm").toInt(), 1510);
    QCOMPARE(settings->property("recoveryOperation").toString(), QStringLiteral("servo"));
    _mockLink->setParamSetFailureMode(MockLink::FailParamSetNone);
}

void ThrusterMappingIntegrationTest::_functionSaveConfirmationInterlocks_data()
{
    QTest::addColumn<bool>("switchVehicle");
    QTest::newRow("armed-after-confirmation-opens") << false;
    QTest::newRow("active-vehicle-changes-after-confirmation-opens") << true;
}

void ThrusterMappingIntegrationTest::_functionSaveConfirmationInterlocks()
{
    QFETCH(bool, switchVehicle);
    _connectFixture(0x4242);
    QVERIFY(_vehicle && _mockLink);
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    auto* controller = fixture.controller();
    QVERIFY(controller);
    QSignalSpy finished(controller, &ThrusterDirectControlController::functionSaveFinished);
    const QPointer<Vehicle> originalVehicle(_vehicle);
    const QPointer<MockLink> originalLink(_mockLink);
    const auto restoreContext = qScopeGuard([originalVehicle, originalLink]() {
        if (originalLink) {
            originalLink->setArmed(false);
        }
        if (originalVehicle) {
            MultiVehicleManager::instance()->setActiveVehicle(originalVehicle);
            (void) QTest::qWaitFor(
                [originalVehicle]() { return MultiVehicleManager::instance()->activeVehicle() == originalVehicle; },
                1000);
        }
    });
    fixture.window->show();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.window->isExposed(), 3000);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "open"));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.dialog->property("visible").toBool(), 2000);
    QVERIFY(fixture.dialog->property("canConfigureServoFunctions").toBool());
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "requestServoFunctionSave", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(34))));
    QPointer<QObject> confirmation = qvariant_cast<QObject*>(fixture.dialog->property("functionSaveConfirmation"));
    QVERIFY(confirmation && confirmation->property("visible").toBool());
    if (switchVehicle) {
        Vehicle* otherVehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
        QVERIFY(otherVehicle && otherVehicle != originalVehicle);
        MultiVehicleManager::instance()->setActiveVehicle(otherVehicle);
        QTRY_COMPARE_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), otherVehicle, 1000);
        QCOMPARE(controller->vehicle(), originalVehicle.data());
    } else {
        // Change the simulated heartbeat state without sending an ARM command.
        _mockLink->setArmed(true);
        QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 2000);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!fixture.dialog->property("canConfigureServoFunctions").toBool(), 1000);
    QVERIFY(!confirmation->property("acceptButtonEnabled").toBool());
    QVERIFY(!controller->beginFunctionSave(1, 33, 34));
    // Exercise an already queued acceptance even after the button becomes
    // disabled. The actual popup handler must recheck context before any SET.
    QVERIFY(QMetaObject::invokeMethod(confirmation, "_accept"));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    QCOMPARE(finished.count(), 0);
    QCOMPARE(controller->state(), ThrusterDirectControlController::Idle);
    QCOMPARE(_mockLink->paramValue(MAV_COMP_ID_AUTOPILOT1, QStringLiteral("SERVO1_FUNCTION")).toInt(), 33);
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(!fixture.settings()->property("recoveryPending").toBool());
}

void ThrusterMappingIntegrationTest::_desktopDialogTabs()
{
    _connectFixture(0x3030);
    QVERIFY(_vehicle && _mockLink);
    _mockLink->clearReceivedMavCommandCounts();
    _mockLink->clearReceivedMavlinkMessageCounts();
    QSettings oldSettings;
    oldSettings.setValue(QStringLiteral("DeepSharkServoOutputMapping/directServoPwm"), 3000);
    oldSettings.sync();
    {
        MappingDialogFixture legacyFixture;
        QVERIFY2(legacyFixture.dialog, qPrintable(legacyFixture.error));
        QCOMPARE(legacyFixture.settings()->property("directServoPwm").toInt(), 1550);
        QVERIFY(legacyFixture.dialog->property("testStatusText").toString().contains(QStringLiteral("1550")));
        QVERIFY(!legacyFixture.dialog->property("testEnabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(legacyFixture.settings(), "sync"));
    }
    MappingDialogFixture fixture;
    QVERIFY2(fixture.dialog, qPrintable(fixture.error));
    QVERIFY(freshMockTraffic(_mockLink, _vehicle));
    fixture.window->show();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.window->isExposed(), 3000);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "open"));
    QTRY_VERIFY_WITH_TIMEOUT(fixture.dialog->property("visible").toBool(), 3000);
    QObject* tabs = dialogItem(fixture.dialog.get(), QStringLiteral("outputToolTabs"));
    auto* testAction = dialogItem(fixture.dialog.get(), QStringLiteral("outputTestAction1"));
    QObject* functionLabel = dialogItem(fixture.dialog.get(), QStringLiteral("outputFunction1"));
    QVERIFY2(tabs, "Output tab bar must exist in the actual visual tree");
    QVERIFY2(testAction, "Repeater test action must exist in the actual visual tree");
    QVERIFY2(functionLabel, "Function label must exist in the actual visual tree");
    QVERIFY(fixture.controller());
    Fact* function = fixture.controller()->getParameterFact(-1, QStringLiteral("SERVO1_FUNCTION"));
    QVERIFY(function);
    QCOMPARE(function->rawValue().toInt(), 33);
    QVariant functionText;
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "servoFunctionText", Q_RETURN_ARG(QVariant, functionText),
                                      Q_ARG(QVariant, QVariant(1))));
    QCOMPARE(functionLabel->property("text").toString(), functionText.toString());
    QVERIFY(!functionLabel->property("text").toString().contains(QStringLiteral("参数缺失")));
    QVERIFY(!functionLabel->property("text").toString().contains(QStringLiteral("参数不可用")));
    QVERIFY(!fixture.dialog->property("testStatusText").toString().contains(QStringLiteral("连接已断开")));
    QCOMPARE(tabs->property("currentIndex").toInt(), 0);
    QVERIFY(!testAction->isVisible());
    QVERIFY(!fixture.dialog->property("testEnabled").toBool());

    const QStringList names{QStringLiteral("records"), QStringLiteral("motor-test"), QStringLiteral("advanced")};
    QSignalSpy frames(fixture.window.get(), &QQuickWindow::frameSwapped);
    for (int index = 0; index < names.size(); ++index) {
        fixture.dialog->setProperty("testMode", index == 2 ? 1 : 0);
        QVERIFY(tabs->setProperty("currentIndex", index));
        frames.clear();
        fixture.window->requestUpdate();
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        const QImage image = fixture.window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QDir::current().filePath(QStringLiteral("output-tool-%1.png").arg(names.at(index)))));
    }
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST), 0);
    QCOMPARE(_mockLink->receivedMavCommandCount(MAV_CMD_DO_SET_SERVO), 0);
    QCOMPARE(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_PARAM_SET), 0);
    QVERIFY(QMetaObject::invokeMethod(fixture.dialog.get(), "close"));
}

UT_REGISTER_TEST(ThrusterMappingIntegrationTest, TestLabel::Integration)
