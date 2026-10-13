#include "JoystickControlTest.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtTest/QSignalSpy>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "AppSettings.h"
#include "Fact.h"
#include "Joystick.h"
#include "JoystickConfigController.h"
#include "JoystickManager.h"
#include "JoystickManagerSettings.h"
#include "MockJoystick.h"
#include "QGCCorePlugin.h"
#include "SettingsManager.h"
#include "Vehicle.h"

namespace {
QQuickItem* findVisualItem(QQuickItem* item, const QString& name)
{
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = findVisualItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}
}  // namespace

void JoystickControlTest::_manualControlPackets_data()
{
    QTest::addColumn<QList<int>>("axes");
    QTest::addColumn<bool>("reversed");
    QTest::addColumn<bool>("centerZero");
    QTest::addColumn<int>("deadband");
    QTest::addColumn<QList<int>>("expected");  // MAVLink x, y, z, r
    QTest::newRow("center-neutral") << QList<int>{0, 0, 0, 0} << false << true << 0 << QList<int>{0, 0, 0, 0};
    QTest::newRow("axis-direction") << QList<int>{32767, -32768, 16384, -16384} << false << true << 0
                                    << QList<int>{-1000, 1000, -500, 500};
    QTest::newRow("reversed-roll") << QList<int>{32767, 0, 0, 0} << true << true << 0 << QList<int>{0, -1000, 0, 0};
    QTest::newRow("deadband-neutral") << QList<int>{500, 500, 500, 500} << false << true << 1000
                                      << QList<int>{0, 0, 0, 0};
    QTest::newRow("down-zero-throttle") << QList<int>{0, 0, 0, 0} << false << false << 0 << QList<int>{0, 0, 500, 0};
}

void JoystickControlTest::_manualControlPackets()
{
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg, QRegularExpression("showAppMessage:.*Configuration tasks remain"));
    QFETCH(QList<int>, axes);
    QFETCH(bool, reversed);
    QFETCH(bool, centerZero);
    QFETCH(int, deadband);
    QFETCH(QList<int>, expected);
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(waitForInitialConnect());

    auto mock = std::unique_ptr<MockJoystick>(MockJoystick::create(QStringLiteral("ArduSub packet fixture"), 6, 20, 1));
    QVERIFY(mock->isValid());
    auto* const manager = JoystickManager::instance();
    manager->init();
    auto* const managerSettings = SettingsManager::instance()->joystickManagerSettings();
    managerSettings->activeJoystickName()->setRawValue(QStringLiteral("ArduSub packet fixture"));
    QPointer<Joystick> joystick = manager->activeJoystick();
    QVERIFY(joystick);
    QCOMPARE(joystick->name(), QStringLiteral("ArduSub packet fixture"));
    const auto stopJoystick = qScopeGuard([&]() {
        manager->setActiveJoystickEnabledForActiveVehicle(false);
        if (joystick)
            joystick->stop();
        mock.reset();
        manager->init();
    });
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    for (int axis = 0; axis < 4; ++axis) {
        joystick->_setJoystickAxisForAxisFunction(static_cast<Joystick::AxisFunction_t>(axis), axis);
        auto& calibration = joystick->_rgCalibration[axis];
        calibration.reset();
        calibration.deadband = deadband;
        calibration.reversed = axis == 0 && reversed;
        QVERIFY(mock->setAxis(axis, axes[axis]));
    }
    QVERIFY(mock->pressButton(0));
    QVERIFY(mock->pressButton(15));
    QVERIFY(mock->pressButton(19));
    joystick->settings()->calibrated()->setRawValue(true);
    joystick->settings()->useDeadband()->setRawValue(deadband != 0);
    joystick->settings()->throttleModeCenterZero()->setRawValue(centerZero);
    joystick->settings()->negativeThrust()->setRawValue(true);
    joystick->settings()->exponentialPct()->setRawValue(0);
    joystick->settings()->circleCorrection()->setRawValue(false);
    joystick->settings()->throttleSmoothing()->setRawValue(false);

    manager->setActiveJoystickEnabledForActiveVehicle(true);
    mavlink_message_t message{};
    mavlink_manual_control_t control{};
    QTRY_VERIFY_WITH_TIMEOUT(_mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_MANUAL_CONTROL, message), 5000);
    mavlink_msg_manual_control_decode(&message, &control);
    QCOMPARE(control.target, _vehicle->id());
    QCOMPARE(control.x, expected[0]);
    QCOMPARE(control.y, expected[1]);
    QCOMPARE(control.z, expected[2]);
    QCOMPARE(control.r, expected[3]);
    QCOMPARE(control.buttons, 0x8001);
    QCOMPARE(control.buttons2, 8);
    QCOMPARE(control.enabled_extensions, 0);

    // Stop the worker before changing calibration, then exercise the same send guard synchronously.
    joystick->_stopPollingThread();
    joystick->settings()->calibrated()->setRawValue(false);
    QSignalSpy axisSpy(joystick, &Joystick::axisValues);
    QTRY_VERIFY_WITH_TIMEOUT(joystick->_axisElapsedTimer.elapsed() >= 40, 1000);
    joystick->_handleAxis();
    QCOMPARE(axisSpy.count(), 0);
    expectLogMessage("Joystick.Joystick", QtWarningMsg,
                     QRegularExpression("Joystick polling thread not running even though _pollingVehicle was set\\."));
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    verifyExpectedLogMessage();
    QVERIFY(!joystick->isRunning());
    QVERIFY(!joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));

    mock.reset();
    manager->init();
    QVERIFY(!manager->activeJoystick() || manager->activeJoystick()->name() != QStringLiteral("ArduSub packet fixture"));
}

void JoystickControlTest::_configurationKeepsManualControl()
{
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression("showAppMessage:.*Configuration tasks remain"));
    auto* const overrideSetting = SettingsManager::instance()->appSettings()->disableSetupSafetyRestrictions();
    const QVariant previousOverride = overrideSetting->rawValue();
    const auto restoreSetting = qScopeGuard([&]() { overrideSetting->setRawValue(previousOverride); });
    overrideSetting->setRawValue(true);
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(waitForInitialConnect());

    auto mock =
        std::unique_ptr<MockJoystick>(MockJoystick::create(QStringLiteral("Armed configuration fixture"), 6, 20, 0));
    QVERIFY(mock->isValid());
    auto* const manager = JoystickManager::instance();
    manager->init();
    SettingsManager::instance()->joystickManagerSettings()->activeJoystickName()->setRawValue(
        QStringLiteral("Armed configuration fixture"));
    QPointer<Joystick> joystick = manager->activeJoystick();
    QVERIFY(joystick);
    const auto stopJoystick = qScopeGuard([&]() {
        manager->setActiveJoystickEnabledForActiveVehicle(false);
        if (joystick) {
            joystick->stop();
        }
        mock.reset();
        manager->init();
    });
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    for (int axis = 0; axis < 4; ++axis) {
        joystick->_setJoystickAxisForAxisFunction(static_cast<Joystick::AxisFunction_t>(axis), axis);
        joystick->_rgCalibration[axis].reset();
    }
    joystick->settings()->calibrated()->setRawValue(true);
    joystick->settings()->throttleModeCenterZero()->setRawValue(true);
    joystick->settings()->exponentialPct()->setRawValue(0);
    joystick->settings()->circleCorrection()->setRawValue(false);
    joystick->settings()->throttleSmoothing()->setRawValue(false);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    _vehicle->setArmed(true, false);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 5000);

    QQuickItem statusText;
    QQuickItem cancelButton;
    QQuickItem nextButton;
    JoystickConfigController controller;
    QVERIFY(!controller.additionalAxis1Enabled());
    QVERIFY(!controller.additionalAxis6Enabled());
    controller.setProperty("joystick", QVariant::fromValue(joystick.data()));
    controller.setProperty("statusText", QVariant::fromValue(&statusText));
    controller.setProperty("cancelButton", QVariant::fromValue(&cancelButton));
    controller.setProperty("nextButton", QVariant::fromValue(&nextButton));
    controller.setJoystickMode(true);
    QSignalSpy monitorSpy(joystick, &Joystick::rawChannelValuesChanged);
    controller.start();
    const int packetCount = _mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_MANUAL_CONTROL);
    QVERIFY(mock->setAxis(0, 32767));
    QTRY_VERIFY_WITH_TIMEOUT(monitorSpy.count() >= 3, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(_mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_MANUAL_CONTROL) >= packetCount + 3,
                             5000);
    mavlink_message_t message{};
    mavlink_manual_control_t control{};
    QTRY_VERIFY_WITH_TIMEOUT(_mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_MANUAL_CONTROL, message) &&
                                 (mavlink_msg_manual_control_decode(&message, &control), control.y == 1000),
                             5000);
    joystick->setButtonAction(1, joystick->buttonActionNone());
    QSignalSpy buttonMonitorSpy(joystick, &Joystick::rawButtonPressedChanged);
    QVERIFY(mock->pressButton(1));
    QTRY_VERIFY_WITH_TIMEOUT(buttonMonitorSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(_mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_MANUAL_CONTROL, message) &&
                                 (mavlink_msg_manual_control_decode(&message, &control), (control.buttons & 2) != 0),
                             5000);
    QVERIFY(mock->releaseButton(1));

    // The controller guard also applies to direct invocations, independently of the UI.
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg, QRegularExpression("showAppMessage:.*"));
    controller.nextButtonClicked();
    QVERIFY(!controller.calibrating());
    _vehicle->setArmed(false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->armed(), 5000);
    controller.nextButtonClicked();
    QVERIFY(controller.calibrating());
    QVERIFY(!joystick->_configurationAllowsVehicleControl.load());
    controller.cancelButtonClicked();
    QVERIFY(!controller.calibrating());
    QVERIFY(joystick->_configurationAllowsVehicleControl.load());

    // Turning the option off restores configuration-only polling immediately.
    overrideSetting->setRawValue(false);
    QVERIFY(!joystick->_configurationAllowsVehicleControl.load());
    joystick->_stopPollingThread();
    QSignalSpy axisSpy(joystick, &Joystick::axisValues);
    QTRY_VERIFY_WITH_TIMEOUT(joystick->_axisElapsedTimer.elapsed() >= 40, 1000);
    joystick->_handleAxis();
    QCOMPARE(axisSpy.count(), 0);
    overrideSetting->setRawValue(true);
    QTRY_VERIFY_WITH_TIMEOUT(joystick->_axisElapsedTimer.elapsed() >= 40, 1000);
    joystick->_handleAxis();
    QCOMPARE(axisSpy.count(), 1);
    joystick->_startPollingThread();
}

void JoystickControlTest::_firmwareButtonOwnership()
{
    Fact normal(1, QStringLiteral("BTN0_FUNCTION"), FactMetaData::valueTypeInt8);
    Fact shift(1, QStringLiteral("BTN0_SFUNCTION"), FactMetaData::valueTypeInt8);
    Fact secondNormal(1, QStringLiteral("BTN1_FUNCTION"), FactMetaData::valueTypeInt8);
    Fact secondShift(1, QStringLiteral("BTN1_SFUNCTION"), FactMetaData::valueTypeInt8);
    for (Fact* fact : {&normal, &shift, &secondNormal, &secondShift}) {
        fact->setEnumInfo({QStringLiteral("Disabled"), QStringLiteral("Arm"), QStringLiteral("Extended action")},
                          {0, 7, -56});  // ArduSub action 200 represented by signed int8.
        fact->setRawValue(7);
        QQmlEngine::setObjectOwnership(fact, QQmlEngine::CppOwnership);
    }
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QGroundControl
import QGroundControl.Controls
import QGroundControl.VehicleSetup
Item {
    property var normalFact
    property var shiftFact
    property var secondNormalFact
    property var secondShiftFact
    property bool parametersReady: false
    property bool parametersPresent: true
    property int missingReports: 0
    QtObject {
        id: fixtureJoystick
        property int buttonCount: 2
        property var buttonActions: ["QGC action", "No Action"]
        signal rawButtonPressedChanged(int index, bool pressed)
        property string buttonActionNone: "No Action"
        property var assignableActionTitles: ["无操作", "QGC 中文动作"]
        property var assignableActions: ({ get: function(index) { return {canRepeat: false, action: index === 0 ? "No Action" : "QGC action"} } })
        function getButtonRepeat(index) { return false }
        function setButtonRepeat(index, repeat) {}
        function setButtonAction(index, action) {
            const actions = buttonActions.slice()
            actions[index] = action
            buttonActions = actions
        }
    }
    QtObject {
        id: fixtureController
        property var vehicle: ({parameterManager: {parametersReady: parametersReady}, supports: {jsButton: true}})
        function parameterExists(componentId, name) { return parametersPresent }
        function getParameterFact(componentId, name, reportMissing) {
            if (!parametersPresent) { missingReports++; return null }
            if (name.indexOf("BTN1_") === 0) return name.indexOf("SFUNCTION") >= 0 ? secondShiftFact : secondNormalFact
            return name.indexOf("SFUNCTION") >= 0 ? shiftFact : normalFact
        }
    }
    JoystickComponentButtons {
        objectName: "buttonsPage"
        width: 1100
        joystick: fixtureJoystick
        controller: fixtureController
    }
    function currentQgcAction() { return fixtureJoystick.buttonActions[0] }
    function pressButton(index, pressed) { fixtureJoystick.rawButtonPressedChanged(index, pressed) }
    function expandVehicleActions(expanded) {
        fixtureJoystick.assignableActionTitles = expanded ? ["无操作", "新增载具模式", "QGC 中文动作"] : ["无操作", "QGC 中文动作"]
        fixtureJoystick.assignableActions = ({ get: function(index) {
            return {canRepeat: false, action: index === 0 ? "No Action" : (expanded && index === 1 ? "New vehicle mode" : "QGC action")}
        } })
    }
})",
                      QUrl(QStringLiteral("qrc:/joystick-button-fixture.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QVariantMap properties{{QStringLiteral("normalFact"), QVariant::fromValue(&normal)},
                                 {QStringLiteral("shiftFact"), QVariant::fromValue(&shift)},
                                 {QStringLiteral("secondNormalFact"), QVariant::fromValue(&secondNormal)},
                                 {QStringLiteral("secondShiftFact"), QVariant::fromValue(&secondShift)}};
    std::unique_ptr<QObject> root(component.createWithInitialProperties(properties));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* const page = root->findChild<QObject*>(QStringLiteral("buttonsPage"));
    QVERIFY(page);
    auto* const rootItem = qobject_cast<QQuickItem*>(root.get());
    QVERIFY(rootItem);
    QQuickItem* firstRow = nullptr;
    QQuickItem* secondRow = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((firstRow = findVisualItem(rootItem, QStringLiteral("joystickButtonRow0"))), 5000);
    QTRY_VERIFY_WITH_TIMEOUT((secondRow = findVisualItem(rootItem, QStringLiteral("joystickButtonRow1"))), 5000);
    QVERIFY(!qvariant_cast<Fact*>(firstRow->property("_buttonFunction")));
    root->setProperty("parametersReady", true);
    QTRY_COMPARE(qvariant_cast<Fact*>(firstRow->property("_buttonFunction")), &normal);
    QCOMPARE(qvariant_cast<Fact*>(firstRow->property("_shiftFunction")), &shift);
    auto* const qgcCombo = findVisualItem(rootItem, QStringLiteral("joystickQgcActionCombo0"));
    auto* const normalCombo = findVisualItem(rootItem, QStringLiteral("joystickFirmwareActionCombo0"));
    auto* const shiftCombo = findVisualItem(rootItem, QStringLiteral("joystickShiftActionCombo0"));
    QVERIFY(qgcCombo && normalCombo && shiftCombo);
    QCOMPARE(qgcCombo->property("currentIndex").toInt(), 1);
    // Connecting a vehicle can insert mode actions before an existing assignment.
    QVERIFY(QMetaObject::invokeMethod(root.get(), "expandVehicleActions", Q_ARG(QVariant, QVariant(true))));
    QCOMPARE(qgcCombo->property("currentIndex").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "expandVehicleActions", Q_ARG(QVariant, QVariant(false))));
    QCOMPARE(qgcCombo->property("currentIndex").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 1)));
    QCOMPARE(normal.rawValue().toInt(), 0);
    QCOMPARE(shift.rawValue().toInt(), 0);
    QVariant action;
    QVERIFY(QMetaObject::invokeMethod(root.get(), "currentQgcAction", Q_RETURN_ARG(QVariant, action)));
    QCOMPARE(action.toString(), QStringLiteral("QGC action"));  // Store stable IDs, never translated titles.
    QVERIFY(QMetaObject::invokeMethod(normalCombo, "activated", Q_ARG(int, 2)));
    QCOMPARE(normal.rawValue().toInt(), -56);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "currentQgcAction", Q_RETURN_ARG(QVariant, action)));
    QCOMPARE(action.toString(), QStringLiteral("No Action"));
    QCOMPARE(qgcCombo->property("currentIndex").toInt(), 0);
    // Re-selecting "No Action" assigns nothing, so it must leave the vehicle's own functions alone.
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 0)));
    QCOMPARE(normal.rawValue().toInt(), -56);
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 1)));
    QVERIFY(QMetaObject::invokeMethod(shiftCombo, "activated", Q_ARG(int, 2)));
    QCOMPARE(shift.rawValue().toInt(), -56);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "currentQgcAction", Q_RETURN_ARG(QVariant, action)));
    QCOMPARE(action.toString(), QStringLiteral("No Action"));
    QCOMPARE(qgcCombo->property("currentIndex").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 0)));
    QCOMPARE(shift.rawValue().toInt(), -56);
    root->setProperty("parametersPresent", false);
    QTRY_VERIFY(!qvariant_cast<Fact*>(firstRow->property("_buttonFunction")));
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 1)));
    QCOMPARE(shift.rawValue().toInt(), -56);
    QCOMPARE(root->property("missingReports").toInt(), 0);
    QCOMPARE(secondNormal.rawValue().toInt(), 7);
    QCOMPARE(secondShift.rawValue().toInt(), 7);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "pressButton", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(true))));
    QVERIFY(secondRow->property("pressed").toBool());
    QVERIFY(!firstRow->property("pressed").toBool());
    QVERIFY(QMetaObject::invokeMethod(root.get(), "pressButton", Q_ARG(QVariant, QVariant(1)),
                                      Q_ARG(QVariant, QVariant(false))));
    QVERIFY(!secondRow->property("pressed").toBool());
}

void JoystickControlTest::_buttonActionEditsAreSerializedWithPolling()
{
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression("showAppMessage:.*Configuration tasks remain"));
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(waitForInitialConnect());

    auto mock = std::unique_ptr<MockJoystick>(MockJoystick::create(QStringLiteral("Button edit fixture"), 6, 20, 0));
    QVERIFY(mock->isValid());
    auto* const manager = JoystickManager::instance();
    manager->init();
    SettingsManager::instance()->joystickManagerSettings()->activeJoystickName()->setRawValue(
        QStringLiteral("Button edit fixture"));
    QPointer<Joystick> joystick = manager->activeJoystick();
    QVERIFY(joystick);
    const auto stopJoystick = qScopeGuard([&]() {
        manager->setActiveJoystickEnabledForActiveVehicle(false);
        if (joystick) {
            joystick->stop();
        }
        mock.reset();
        manager->init();
    });
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    for (int axis = 0; axis < 4; ++axis) {
        joystick->_setJoystickAxisForAxisFunction(static_cast<Joystick::AxisFunction_t>(axis), axis);
        joystick->_rgCalibration[axis].reset();
    }
    joystick->settings()->calibrated()->setRawValue(true);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));

    // Stress regression for the assignment objects: replace the 50 Hz worker with a fast loop over the
    // same handler so that the page's edits overlap the vehicle-control scan. It cannot force the exact
    // interleaving, and with no button held it does not reach the repeat timer or the queued actions.
    joystick->_stopPollingThread();
    QVERIFY(joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));
    std::atomic_bool stopPolling{false};
    std::atomic<quint64> pollCount{0};
    std::thread polling([&]() {
        while (!stopPolling.load()) {
            joystick->_handleButtons();
            pollCount.fetch_add(1);
            std::this_thread::yield();
        }
    });
    const auto stopWorker = qScopeGuard([&]() {
        stopPolling.store(true);
        if (polling.joinable()) {
            polling.join();
        }
    });
    QElapsedTimer budget;
    budget.start();
    // Two more completed scans guarantee that one of them started after the caller's last edit.
    const auto waitForScan = [&]() {
        const quint64 seen = pollCount.load();
        while (pollCount.load() < seen + 2 && budget.elapsed() < 30000) {
            std::this_thread::yield();
        }
        return pollCount.load() >= seen + 2;
    };
    QVERIFY2(waitForScan(), "The polling loop did not start");
    const QString action = QStringLiteral("Button edit fixture action");
    for (int edit = 0; edit < 200; ++edit) {
        joystick->setButtonAction(1, action);
        joystick->setButtonRepeat(1, true);
        QVERIFY2(waitForScan(), "The polling loop made no progress while an assignment existed");
        joystick->setButtonAction(1, joystick->buttonActionNone());
    }
    stopPolling.store(true);
    polling.join();
    QCOMPARE(joystick->getButtonAction(1), joystick->buttonActionNone());
    joystick->_startPollingThread();
}

void JoystickControlTest::_pollingStateChangesAreSerializedWithPolling()
{
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression("showAppMessage:.*Configuration tasks remain"));
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(waitForInitialConnect());

    auto mock = std::unique_ptr<MockJoystick>(MockJoystick::create(QStringLiteral("Polling state fixture"), 6, 20, 0));
    QVERIFY(mock->isValid());
    auto* const manager = JoystickManager::instance();
    manager->init();
    SettingsManager::instance()->joystickManagerSettings()->activeJoystickName()->setRawValue(
        QStringLiteral("Polling state fixture"));
    QPointer<Joystick> joystick = manager->activeJoystick();
    QVERIFY(joystick);
    const auto stopJoystick = qScopeGuard([&]() {
        manager->setActiveJoystickEnabledForActiveVehicle(false);
        if (joystick) {
            joystick->stop();
        }
        mock.reset();
        manager->init();
    });
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    for (int axis = 0; axis < 4; ++axis) {
        joystick->_setJoystickAxisForAxisFunction(static_cast<Joystick::AxisFunction_t>(axis), axis);
        joystick->_rgCalibration[axis].reset();
    }
    joystick->settings()->calibrated()->setRawValue(true);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));

    // Stress regression: the configuration page stays open (its flag keeps the worker alive) while the
    // vehicle side is dropped and picked up again, as on a vehicle switch or disconnect. The worker must
    // never see "polling for a vehicle" without a vehicle, which it reports as an internal error and
    // which strict log checking turns into a failure. The exact interleaving cannot be forced.
    QSignalSpy firstScan(joystick, &Joystick::rawChannelValuesChanged);
    joystick->_startPollingForConfiguration();
    joystick->_configurationAllowsVehicleControl = true; // As with "disable safety restrictions".
    QVERIFY(joystick->isRunning());
    // A running thread may still be opening the device. The channel values are reported from a scan, so
    // one report means that the device is open and the first scan has begun.
    QVERIFY(!firstScan.isEmpty() || firstScan.wait(10000));
    const auto stopConfiguration = qScopeGuard([&]() {
        if (joystick && joystick->_pollingFlags.testFlag(Joystick::PollingForConfiguration)) {
            joystick->_stopPollingForConfiguration();
        }
    });
    // The joystick's own 50 Hz worker keeps running; a second, fast scanner over the same handler makes
    // the scans overlap the state changes often enough to matter.
    std::atomic_bool stopPolling{false};
    std::atomic<quint64> pollCount{0};
    std::thread polling([&]() {
        while (!stopPolling.load()) {
            joystick->_handleButtons();
            pollCount.fetch_add(1);
        }
    });
    const auto stopWorker = qScopeGuard([&]() {
        stopPolling.store(true);
        if (polling.joinable()) {
            polling.join();
        }
    });
    QElapsedTimer budget;
    budget.start();
    while (pollCount.load() < 2 && budget.elapsed() < 30000) {
        std::this_thread::yield();
    }
    QVERIFY2(pollCount.load() >= 2, "The polling loop did not start");

    Vehicle* const vehicle = _vehicle;
    for (int change = 0; change < 3000; ++change) {
        joystick->_stopAllPollingForVehicle();
        QVERIFY(!joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));
        // Pick the vehicle up again without the signal connections, which this test does not need.
        const QMutexLocker locker(&joystick->_pollingStateMutex);
        joystick->_pollingVehicle = vehicle;
        joystick->_pollingFlags |= Joystick::PollingForVehicle;
    }
    const quint64 seen = pollCount.load();
    while (pollCount.load() < seen + 2 && budget.elapsed() < 30000) {
        std::this_thread::yield();
    }
    stopPolling.store(true);
    polling.join();
    QVERIFY(pollCount.load() >= seen + 2);
    QVERIFY(joystick->isRunning());
}

void JoystickControlTest::_startLiveJoystick(const QString &name, std::unique_ptr<MockJoystick> &mock,
                                             QPointer<Joystick> &joystick)
{
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression("showAppMessage:.*Configuration tasks remain"));
    // The mock vehicle has no gimbal. The gimbal controller reports that for every start and stop, exactly
    // as it does when such a button is pressed and released normally.
    ignoreLogMessage("Gimbal.GimbalController", QtCriticalMsg,
                     QRegularExpression("gimbal(Pitch|Yaw)(Start|Stop): active gimbal is nullptr"));
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(waitForInitialConnect());

    mock.reset(MockJoystick::create(name, 6, 20, 0));
    QVERIFY(mock->isValid());
    auto* const manager = JoystickManager::instance();
    manager->init();
    SettingsManager::instance()->joystickManagerSettings()->activeJoystickName()->setRawValue(name);
    joystick = manager->activeJoystick();
    QVERIFY(joystick);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    for (int axis = 0; axis < 4; ++axis) {
        joystick->_setJoystickAxisForAxisFunction(static_cast<Joystick::AxisFunction_t>(axis), axis);
        joystick->_rgCalibration[axis].reset();
    }
    joystick->settings()->calibrated()->setRawValue(true);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));
}

void JoystickControlTest::_stopLiveJoystick(std::unique_ptr<MockJoystick> &mock, const QPointer<Joystick> &joystick)
{
    auto* const manager = JoystickManager::instance();
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    if (joystick) {
        joystick->stop();
    }
    mock.reset();
    manager->init();
}

bool JoystickControlTest::_scanHasSeen(Joystick *joystick, int button, bool pressed)
{
    // Waits, without running the event loop, until a scan has seen the button pressed, or has seen it
    // released and gone back to idle. A scan holds the polling state lock from reading the buttons to
    // queueing their actions, so once this returns the action of that scan is queued and has not run yet.
    // A press that arrives before the button is idle again is not seen as a new press at all.
    QElapsedTimer budget;
    budget.start();
    while (budget.elapsed() < 30000) {
        {
            const QMutexLocker locker(&joystick->_pollingStateMutex);
            const Joystick::ButtonEvent_t state = joystick->_buttonEventStates[button];
            const bool seenPressed =
                (state == Joystick::ButtonEventDownTransition) || (state == Joystick::ButtonEventRepeat);
            if (pressed ? seenPressed : (state == Joystick::ButtonEventNone)) {
                return true;
            }
        }
        // Sleeping outside the lock leaves it to the polling thread.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void JoystickControlTest::_reassignmentSettlesOnlyWhatTheButtonHolds()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Reassign fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    const QString zoomIn = QString::fromLatin1(Joystick::_buttonActionContinuousZoomIn);
    const QString zoomOut = QString::fromLatin1(Joystick::_buttonActionContinuousZoomOut);
    QSignalSpy zoomStarts(joystick, &Joystick::startContinuousZoom);
    QSignalSpy zoomStops(joystick, &Joystick::stopContinuousZoom);

    // Button 1 moves the gimbal up and is held. Button 2 has "gimbal down" and is not pressed; both actions
    // end with the same pitch stop.
    joystick->setButtonAction(1, gimbalUp);
    joystick->setButtonAction(2, gimbalDown);
    QCOMPARE(pitchStops.count(), 0);
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);

    // Taking the action away from the idle button must not stop what the held button is doing.
    joystick->setButtonAction(2, none);
    QCOMPARE(pitchStops.count(), 0);

    // The release handler is looked up by the action assigned at release time. Once the action of the held
    // button is replaced, nothing would ever run its stop, so the edit itself has to. The new action is
    // not started by a button that was already down.
    joystick->setButtonAction(1, gimbalLeft);
    QCOMPARE(pitchStops.count(), 1);
    QCOMPARE(yawStops.count(), 0);
    QCOMPARE(yawStarts.count(), 0);

    // The stop is sent once. Another edit of the same button has nothing left to settle.
    joystick->setButtonAction(1, gimbalUp);
    QCOMPARE(pitchStops.count(), 1);
    QCOMPARE(yawStops.count(), 0);
    QVERIFY(releaseAndSettle(1));

    // A later press on the same control takes it over. Button 1 went up first, button 2 then went down,
    // and both are held: the pitch rate is the one of button 2. Editing button 1 must leave it alone;
    // editing button 2 ends it.
    joystick->setButtonAction(2, gimbalDown);
    pitchStarts.clear();
    pitchStops.clear();
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    QVERIFY(mock->pressButton(2));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 2, 10000);
    QCOMPARE(pitchStarts.at(1).at(0).toInt(), -1);
    joystick->setButtonAction(1, none);
    QCOMPARE(pitchStops.count(), 0);
    joystick->setButtonAction(2, none);
    QCOMPARE(pitchStops.count(), 1);
    QVERIFY(releaseAndSettle(1));
    QVERIFY(releaseAndSettle(2));

    // The same holds for the camera zoom, another pair of actions with a common stop.
    joystick->setButtonAction(1, zoomIn);
    joystick->setButtonAction(2, zoomOut);
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(zoomStarts.count(), 1, 10000);
    QVERIFY(mock->pressButton(2));
    QTRY_COMPARE_WITH_TIMEOUT(zoomStarts.count(), 2, 10000);
    joystick->setButtonAction(1, none);
    QCOMPARE(zoomStops.count(), 0);
    joystick->setButtonAction(2, none);
    QCOMPARE(zoomStops.count(), 1);
    QVERIFY(releaseAndSettle(1));
    QVERIFY(releaseAndSettle(2));
}

void JoystickControlTest::_guardIsReadWhenAButtonIsEdited()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Guard fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    // A press that a scan has queued, and that has not run yet, belongs to the assignment it was read
    // from. After that assignment is replaced it must not run: no release would end it.
    joystick->setButtonAction(1, gimbalUp);
    QVERIFY(mock->pressButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, true));
    QCOMPARE(pitchStarts.count(), 0);
    joystick->setButtonAction(1, gimbalLeft);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStarts.count(), 0);
    QCOMPARE(yawStarts.count(), 0);
    QCOMPARE(pitchStops.count(), 0);
    QVERIFY(releaseAndSettle(1));

    // The guard can be switched off in the application settings. The same sequence then behaves as it
    // did before: the queued press runs and the edit releases nothing.
    guard->setRawValue(false);
    joystick->setButtonAction(1, gimbalUp);
    pitchStarts.clear();
    pitchStops.clear();
    QVERIFY(mock->pressButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, true));
    joystick->setButtonAction(1, gimbalLeft);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStarts.count(), 1);
    QCOMPARE(pitchStops.count(), 0);
    QVERIFY(releaseAndSettle(1));

    // The guard is read when the edit is made, not when a queued event runs. An edit without the guard
    // stops nothing, so the release that was already queued for the old action has to stay valid even if
    // the guard is switched on before it runs. Otherwise neither of the two would end the motion.
    joystick->setButtonAction(1, gimbalUp);
    pitchStarts.clear();
    pitchStops.clear();
    yawStops.clear();
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    QVERIFY(mock->releaseButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, false));
    QCOMPARE(pitchStops.count(), 0);
    joystick->setButtonAction(1, gimbalLeft);
    QCOMPARE(pitchStops.count(), 0);
    guard->setRawValue(true);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStops.count(), 1);
    QCOMPARE(yawStops.count(), 0);
}

void JoystickControlTest::_leavingAVehicleSettlesWhatTheJoystickStarted()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Leave fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    // The joystick stops controlling the vehicle while a button holds the gimbal. The button stays down,
    // so no release will be delivered: the stop has to end what the joystick started.
    joystick->setButtonAction(1, gimbalUp);
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QCOMPARE(pitchStops.count(), 1);

    // After it controls the vehicle again the button is still down. That is no new press, and an edit has
    // nothing left to settle.
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));
    QVERIFY(_scanHasSeen(joystick, 1, true));
    QCoreApplication::processEvents();
    QCOMPARE(pitchStarts.count(), 1);
    joystick->setButtonAction(1, gimbalLeft);
    QCOMPARE(pitchStops.count(), 1);
    QCOMPARE(yawStarts.count(), 0);
    QVERIFY(releaseAndSettle(1));
    QCOMPARE(pitchStarts.count(), 1);

    // Without the guard nothing is ended for a button that is still down, as before.
    guard->setRawValue(false);
    joystick->setButtonAction(1, gimbalUp);
    pitchStarts.clear();
    pitchStops.clear();
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QCOMPARE(pitchStops.count(), 0);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(releaseAndSettle(1));
}

void JoystickControlTest::_actionsQueuedForAnEarlierSessionDoNotRun()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Session fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    QSignalSpy armRequests(joystick, &Joystick::setArmed);
    joystick->setButtonAction(1, gimbalUp);
    joystick->setButtonAction(2, gimbalDown);
    joystick->setButtonAction(3, QString::fromLatin1(Joystick::_buttonActionArm));

    // An action that was queued while the joystick controlled the vehicle must not run once that has
    // ended, even when the joystick controls a vehicle again by the time the event is delivered. This is
    // an arm request read in the old session.
    QVERIFY(mock->pressButton(3));
    QVERIFY(_scanHasSeen(joystick, 3, true));
    QCOMPARE(armRequests.count(), 0);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QCoreApplication::processEvents();
    QCOMPARE(armRequests.count(), 0);
    QVERIFY(releaseAndSettle(3));
    QCOMPARE(armRequests.count(), 0);

    // A release is different: the button really was released, and nothing will report that again. With
    // the guard off, nothing else ends the motion either, so the release that a scan has already queued
    // has to be completed when its session ends. It is completed exactly once.
    guard->setRawValue(false);
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    QVERIFY(mock->releaseButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, false));
    QCOMPARE(pitchStops.count(), 0);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QCOMPARE(pitchStops.count(), 1);

    // The queued event of that release is still waiting. It must not run in the new session, where it
    // would stop what another button has started since.
    QVERIFY(mock->pressButton(2));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 2, 10000);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStops.count(), 1);
    QVERIFY(releaseAndSettle(2));
    QCOMPARE(pitchStops.count(), 2);
}

void JoystickControlTest::_heldActionIsNotSettledOnAnotherDevice()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Device fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    // The stop of a continuous action goes to whichever gimbal or camera is selected when it is sent. The
    // mock vehicle has neither, so the selection is supplied here.
    QObject firstDevice;
    QObject secondDevice;
    QObject *selectedDevice = &firstDevice;
    joystick->_deviceForContinuousControlOverride = [&selectedDevice](const QString &) { return selectedDevice; };
    const auto clearOverride = qScopeGuard([&]() {
        if (joystick) {
            joystick->_deviceForContinuousControlOverride = nullptr;
        }
    });

    // Started on the first device and still held when the selection changes: a stop sent now would reach
    // the second device, which this joystick never moved. Neither an edit nor the end of the session may
    // send it.
    joystick->setButtonAction(1, gimbalUp);
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    selectedDevice = &secondDevice;
    joystick->setButtonAction(1, gimbalLeft);
    QCOMPARE(pitchStops.count(), 0);
    QVERIFY(releaseAndSettle(1));

    joystick->setButtonAction(1, gimbalUp);
    selectedDevice = &firstDevice;
    pitchStarts.clear();
    pitchStops.clear();
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    selectedDevice = &secondDevice;
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QCOMPARE(pitchStops.count(), 0);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(releaseAndSettle(1));

    // With the selection unchanged both still settle.
    selectedDevice = &firstDevice;
    pitchStarts.clear();
    pitchStops.clear();
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QCOMPARE(pitchStops.count(), 1);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(releaseAndSettle(1));

    // A release that is still queued when the session ends is completed there. That stop is subject to
    // the same check: started on the first device, released after the second was selected.
    pitchStarts.clear();
    pitchStops.clear();
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    selectedDevice = &secondDevice;
    QVERIFY(mock->releaseButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, false));
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QCOMPARE(pitchStops.count(), 0);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStops.count(), 0);
}

void JoystickControlTest::_releaseWithoutADispatchedPressIsNotCompleted()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Undispatched fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    // A press and its release are both queued when the session ends. The press is dropped with the
    // session, so its release has nothing to end: completing it would send a stop for a motion this
    // button never started, to a gimbal that something else may be moving.
    guard->setRawValue(false);
    joystick->setButtonAction(1, gimbalUp);
    QVERIFY(mock->pressButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, true));
    QVERIFY(mock->releaseButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, false));
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStarts.count(), 0);
    QCOMPARE(pitchStops.count(), 0);
}

void JoystickControlTest::_releaseOfAPressFromAnEarlierSessionIsNotCompleted()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Carried press fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }

    auto* const manager = JoystickManager::instance();
    auto* const guard = SettingsManager::instance()->appSettings()->joystickSetupLiveControlGuard();
    const QVariant previousGuard = guard->rawValue();
    const auto restoreGuard = qScopeGuard([&]() { guard->setRawValue(previousGuard); });
    guard->setRawValue(true);

    const QString gimbalUp = QString::fromLatin1(Joystick::_buttonActionGimbalUp);
    const QString gimbalDown = QString::fromLatin1(Joystick::_buttonActionGimbalDown);
    const QString gimbalLeft = QString::fromLatin1(Joystick::_buttonActionGimbalLeft);
    const QString none = joystick->buttonActionNone();
    QSignalSpy pitchStarts(joystick, &Joystick::gimbalPitchStart);
    QSignalSpy pitchStops(joystick, &Joystick::gimbalPitchStop);
    QSignalSpy yawStarts(joystick, &Joystick::gimbalYawStart);
    QSignalSpy yawStops(joystick, &Joystick::gimbalYawStop);
    // Releases the button, waits until a scan has queued the release and runs what is queued.
    const auto releaseAndSettle = [&](int button) {
        const bool released = mock->releaseButton(button) && _scanHasSeen(joystick, button, false);
        QCoreApplication::processEvents();
        return released;
    };
    Q_UNUSED(manager)
    Q_UNUSED(gimbalDown)
    Q_UNUSED(gimbalLeft)
    Q_UNUSED(none)
    Q_UNUSED(yawStarts)
    Q_UNUSED(yawStops)
    Q_UNUSED(releaseAndSettle)

    // Button 1 is pressed in one session and stays down into the next one, where button 2 takes the
    // pitch over. The release of button 1 is scanned in the second session, but its press is not of that
    // session: completing it when the session ends would stop what button 2 holds. Without the guard
    // nothing is settled for a button that is still down, so no stop goes out at all.
    guard->setRawValue(false);
    joystick->setButtonAction(1, gimbalUp);
    joystick->setButtonAction(2, gimbalDown);
    QVERIFY(mock->pressButton(1));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 1, 10000);
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QCOMPARE(pitchStops.count(), 0);
    QVERIFY(mock->pressButton(2));
    QTRY_COMPARE_WITH_TIMEOUT(pitchStarts.count(), 2, 10000);
    QVERIFY(mock->releaseButton(1));
    QVERIFY(_scanHasSeen(joystick, 1, false));
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QCOMPARE(pitchStops.count(), 0);
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QCoreApplication::processEvents();
    QCOMPARE(pitchStops.count(), 0);
    QVERIFY(releaseAndSettle(2));
}

void JoystickControlTest::_transmitterModeAppliesWithoutCalibration()
{
    std::unique_ptr<MockJoystick> mock;
    QPointer<Joystick> joystick;
    const auto stopJoystick = qScopeGuard([&]() { _stopLiveJoystick(mock, joystick); });
    _startLiveJoystick(QStringLiteral("Transmitter mode fixture"), mock, joystick);
    if (QTest::currentTestFailed()) {
        return;
    }
    auto* const manager = JoystickManager::instance();
    Fact* const storedMode = joystick->settings()->transmitterMode();
    const QVariant previousMode = storedMode->rawValue();
    const auto restoreMode = qScopeGuard([&]() {
        if (joystick) {
            joystick->settings()->transmitterMode()->setRawValue(previousMode);
        }
    });
    storedMode->setRawValue(2);
    const auto axisFor = [&](Joystick::AxisFunction_t function) {
        return joystick->_getJoystickAxisForAxisFunction(function);
    };
    const auto mainAxes = [&]() {
        return QList<int>{axisFor(Joystick::rollFunction), axisFor(Joystick::pitchFunction),
                          axisFor(Joystick::yawFunction), axisFor(Joystick::throttleFunction)};
    };
    const QList<int> mode2Axes{0, 1, 2, 3};
    // Mode 1 has pitch and throttle on the other stick each; roll and yaw stay where they are.
    const QList<int> mode1Axes{0, 3, 2, 1};
    QCOMPARE(mainAxes(), mode2Axes);

    JoystickConfigController controller;
    controller.setProperty("joystick", QVariant::fromValue(joystick.data()));
    QCOMPARE(controller.transmitterMode(), 2);

    // The joystick is sending its sticks to the vehicle, and one of them is deflected. The vehicle is not
    // known to be armed, which is all the ground station can know right after an arm command. The sticks
    // must keep their meaning: nothing is stored, nothing is remapped, and the packets stay as they were.
    QVERIFY(mock->setAxis(1, 32767));
    mavlink_message_t message{};
    mavlink_manual_control_t before{};
    QTRY_VERIFY_WITH_TIMEOUT(_mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_MANUAL_CONTROL, message) &&
                                 (mavlink_msg_manual_control_decode(&message, &before), before.x != 0),
                             5000);
    QVERIFY(!_vehicle->armed());
    const auto packetsKeepTheirMeaning = [&]() {
        const int packetCount = _mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_MANUAL_CONTROL);
        if (!QTest::qWaitFor(
                [&]() {
                    return _mockLink->receivedMavlinkMessageCount(MAVLINK_MSG_ID_MANUAL_CONTROL) >= packetCount + 3;
                },
                5000)) {
            return false;
        }
        mavlink_manual_control_t after{};
        return _mockLink->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_MANUAL_CONTROL, message) &&
               (mavlink_msg_manual_control_decode(&message, &after), after.x == before.x && after.z == before.z);
    };
    expectAppMessage(QRegularExpression(QStringLiteral("手柄控制处于启用状态")));
    controller.setTransmitterMode(1);
    verifyExpectedLogMessage();
    QCOMPARE(controller.transmitterMode(), 2);
    QCOMPARE(storedMode->rawValue().toInt(), 2);
    QCOMPARE(mainAxes(), mode2Axes);
    QVERIFY(packetsKeepTheirMeaning());

    // The configuration page suspends control of the vehicle while it is open. That is not the same as
    // control being off: the page closes by itself when the armed state arrives, and the joystick is back
    // at once. A selection made in between is refused too, so the packets that follow the page mean what
    // they meant before it.
    joystick->_startPollingForConfiguration();
    QVERIFY(!joystick->_configurationAllowsVehicleControl.load());
    expectAppMessage(QRegularExpression(QStringLiteral("手柄控制处于启用状态")));
    controller.setTransmitterMode(1);
    verifyExpectedLogMessage();
    QCOMPARE(controller.transmitterMode(), 2);
    QCOMPARE(storedMode->rawValue().toInt(), 2);
    QCOMPARE(mainAxes(), mode2Axes);
    joystick->_stopPollingForConfiguration();
    QVERIFY(packetsKeepTheirMeaning());

    // With joystick control turned off a selection is enough: it is stored and the sticks are remapped,
    // with no calibration. Loading the stored settings again, as a restart does, gives the assignments
    // that are already in use.
    QVERIFY(mock->setAxis(1, 0));
    manager->setActiveJoystickEnabledForActiveVehicle(false);
    QVERIFY(!joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));
    joystick->_saveFromCalibrationDataIntoSettings();
    // Another vehicle still has the joystick enabled. It would be controlled with the new assignments the
    // moment it becomes the active one, so the change ends that permission as well.
    Fact* const enabledVehicles =
        SettingsManager::instance()->joystickManagerSettings()->joystickEnabledVehiclesIds();
    enabledVehicles->setRawValue(QStringLiteral("77"));
    expectAppMessage(QRegularExpression(QStringLiteral("其他机器人上的手柄控制也已停用")));
    controller.setTransmitterMode(1);
    verifyExpectedLogMessage();
    QVERIFY(enabledVehicles->rawValue().toString().isEmpty());
    QVERIFY(!manager->activeJoystickEnabledForActiveVehicle());
    QCOMPARE(storedMode->rawValue().toInt(), 1);
    QCOMPARE(controller.transmitterMode(), 1);
    QCOMPARE(mainAxes(), mode1Axes);
    joystick->_loadFromSettingsIntoCalibrationData();
    QVERIFY(joystick->settings()->calibrated()->rawValue().toBool());
    QCOMPARE(mainAxes(), mode1Axes);

    // A calibration that was invalidated leaves its assignments behind. They follow the mode too, so the
    // monitor does not show them under the name of another mode; the joystick stays uncalibrated.
    joystick->settings()->calibrated()->setRawValue(false);
    controller.setTransmitterMode(2);
    QCOMPARE(storedMode->rawValue().toInt(), 2);
    QCOMPARE(mainAxes(), mode2Axes);
    QVERIFY(!joystick->settings()->calibrated()->rawValue().toBool());
    joystick->settings()->calibrated()->setRawValue(true);

    // With the setup safety restrictions off the configuration page controls the vehicle on purpose, and the
    // operator who switched them off also decides about the mode: it changes with the joystick enabled, and
    // the joystick stays enabled.
    auto* const overrideSetting = SettingsManager::instance()->appSettings()->disableSetupSafetyRestrictions();
    const QVariant previousOverride = overrideSetting->rawValue();
    const auto restoreOverride = qScopeGuard([&]() { overrideSetting->setRawValue(previousOverride); });
    manager->setActiveJoystickEnabledForActiveVehicle(true);
    QVERIFY(joystick->_pollingFlags.testFlag(Joystick::PollingForVehicle));
    overrideSetting->setRawValue(true);
    QVERIFY(_vehicle->setupSafetyRestrictionsDisabled());
    controller.setTransmitterMode(1);
    QCOMPARE(storedMode->rawValue().toInt(), 1);
    QCOMPARE(mainAxes(), mode1Axes);
    QVERIFY(manager->activeJoystickEnabledForActiveVehicle());
    controller.setTransmitterMode(2);
    QCOMPARE(storedMode->rawValue().toInt(), 2);
    QCOMPARE(mainAxes(), mode2Axes);
    overrideSetting->setRawValue(false);
    manager->setActiveJoystickEnabledForActiveVehicle(false);

    // A vehicle that is known to be armed refuses the selection whether or not the joystick controls it.
    _vehicle->setArmed(true, false);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 5000);
    expectAppMessage(QRegularExpression(QStringLiteral("请先上锁，再切换摇杆模式")));
    controller.setTransmitterMode(1);
    verifyExpectedLogMessage();
    QCOMPARE(controller.transmitterMode(), 2);
    QCOMPARE(storedMode->rawValue().toInt(), 2);
    QCOMPARE(mainAxes(), mode2Axes);
    _vehicle->setArmed(false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->armed(), 5000);
}

UT_REGISTER_TEST(JoystickControlTest, TestLabel::Unit, TestLabel::Joystick)
