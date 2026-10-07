#include "JoystickControlTest.h"

#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QSignalSpy>
#include <memory>

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
    for (Fact* fact : {&normal, &shift}) {
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
    property bool parametersReady: false
    property bool parametersPresent: true
    property int missingReports: 0
    QtObject {
        id: fixtureJoystick
        property int buttonCount: 1
        property var buttonActions: ["QGC action"]
        property string buttonActionNone: "No Action"
        property var assignableActionTitles: ["无操作", "QGC 中文动作"]
        property var assignableActions: ({ get: function(index) { return {canRepeat: false, action: index === 0 ? "No Action" : "QGC action"} } })
        function getButtonRepeat(index) { return false }
        function setButtonRepeat(index, repeat) {}
        function setButtonAction(index, action) { buttonActions = [action] }
    }
    QtObject {
        id: fixtureController
        property var vehicle: ({parameterManager: {parametersReady: parametersReady}, supports: {jsButton: true}})
        function parameterExists(componentId, name) { return parametersPresent }
        function getParameterFact(componentId, name, reportMissing) {
            if (!parametersPresent) { missingReports++; return null }
            return name.indexOf("SFUNCTION") >= 0 ? shiftFact : normalFact
        }
    }
    JoystickComponentButtons {
        objectName: "buttonsPage"
        joystick: fixtureJoystick
        controller: fixtureController
    }
    function currentQgcAction() { return fixtureJoystick.buttonActions[0] }
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
                                 {QStringLiteral("shiftFact"), QVariant::fromValue(&shift)}};
    std::unique_ptr<QObject> root(component.createWithInitialProperties(properties));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* const page = root->findChild<QObject*>(QStringLiteral("buttonsPage"));
    QVERIFY(page);
    QVERIFY(!qvariant_cast<Fact*>(page->property("_buttonFunction")));
    root->setProperty("parametersReady", true);
    QTRY_COMPARE(qvariant_cast<Fact*>(page->property("_buttonFunction")), &normal);
    QCOMPARE(qvariant_cast<Fact*>(page->property("_shiftFunction")), &shift);
    auto* const qgcCombo = root->findChild<QObject*>(QStringLiteral("joystickQgcActionCombo"));
    auto* const normalCombo = root->findChild<QObject*>(QStringLiteral("joystickFirmwareActionCombo"));
    auto* const shiftCombo = root->findChild<QObject*>(QStringLiteral("joystickShiftActionCombo"));
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
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 1)));
    QVERIFY(QMetaObject::invokeMethod(shiftCombo, "activated", Q_ARG(int, 2)));
    QCOMPARE(shift.rawValue().toInt(), -56);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "currentQgcAction", Q_RETURN_ARG(QVariant, action)));
    QCOMPARE(action.toString(), QStringLiteral("No Action"));
    QCOMPARE(qgcCombo->property("currentIndex").toInt(), 0);
    root->setProperty("parametersPresent", false);
    QTRY_VERIFY(!qvariant_cast<Fact*>(page->property("_buttonFunction")));
    QVERIFY(QMetaObject::invokeMethod(qgcCombo, "activated", Q_ARG(int, 1)));
    QCOMPARE(shift.rawValue().toInt(), -56);
    QCOMPARE(root->property("missingReports").toInt(), 0);
}

UT_REGISTER_TEST(JoystickControlTest, TestLabel::Unit, TestLabel::Joystick)
