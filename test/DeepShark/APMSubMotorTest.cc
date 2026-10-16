#include "APMSubMotorTest.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <memory>

#include "AppSettings.h"
#include "ColoredSvgImageProvider.h"
#include "Fact.h"
#include "ParameterManager.h"
#include "QGCCorePlugin.h"
#include "SettingsManager.h"
#include "Vehicle.h"

namespace {
QQuickItem* visualItem(QQuickItem* item, const QString& name)
{
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = visualItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}
}  // namespace

void APMSubMotorTest::_manualTestStates_data()
{
    QTest::addColumn<bool>("overrideEnabled");
    QTest::addColumn<bool>("initiallyArmed");
    QTest::newRow("normal-disarmed") << false << false;
    QTest::newRow("normal-armed") << false << true;
    QTest::newRow("override-disarmed") << true << false;
    QTest::newRow("override-armed") << true << true;
}

void APMSubMotorTest::_manualTestStates()
{
    QFETCH(bool, overrideEnabled);
    QFETCH(bool, initiallyArmed);
    auto* const setting = SettingsManager::instance()->appSettings()->disableSetupSafetyRestrictions();
    const QVariant originalSetting = setting->rawValue();
    const auto restoreSetting = qScopeGuard([&]() { setting->setRawValue(originalSetting); });
    setting->setRawValue(overrideEnabled);
    _mockLink = MockLink::startAPMArduSubMockLink();
    QVERIFY(_mockLink);
    connect(_mockLink, &QObject::destroyed, this, [this]() { _mockLink = nullptr; });
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(_vehicle);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->parameterManager()->parametersReady(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->isInitialConnectComplete(), 10000);
    if (initiallyArmed) {
        _vehicle->setArmed(true, false);
        QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 5000);
    }

    QList<float> motorPercents;
    QObject commandObserver;
    connect(_mockLink, &MockLink::writeBytesQueuedSignal, &commandObserver, [&](const QByteArray& bytes) {
        mavlink_message_t receiveBuffer{};
        mavlink_status_t receiveStatus{};
        mavlink_message_t message{};
        mavlink_status_t status{};
        for (const char byte : bytes) {
            if (mavlink_frame_char_buffer(&receiveBuffer, &receiveStatus, static_cast<uint8_t>(byte), &message,
                                          &status) != MAVLINK_FRAMING_OK ||
                message.msgid != MAVLINK_MSG_ID_COMMAND_LONG) {
                continue;
            }
            mavlink_command_long_t command{};
            mavlink_msg_command_long_decode(&message, &command);
            if (command.command == MAV_CMD_DO_MOTOR_TEST) {
                motorPercents.append(command.param3);
            }
        }
    });

    // Load the production page with its real controller and MockLink, without unrelated flight/video UI.
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("fixtureVehicle"), _vehicle);
    // The production frame display prints its resource URL on creation.
    ignoreLogMessage("qml", QtDebugMsg, QRegularExpression(QStringLiteral("^qrc:///qmlimages/Frames/Vectored\\.png$")));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QGroundControl
import QGroundControl.Controls
import QGroundControl.AutoPilotPlugins.APM
Item {
    width: 1400
    height: 1000
    property var vehicleComponent: null
    property QtObject globals: QtObject { property var activeVehicle: fixtureVehicle }
    QGCPalette { id: qgcPal }
    APMSubMotorComponent { objectName: "motorPage"; anchors.fill: parent }
})",
                      QUrl(QStringLiteral("qrc:/sub-motor-test.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* const rootItem = qobject_cast<QQuickItem*>(root.get());
    QVERIFY(rootItem);
    QQuickWindow window;
    window.resize(1400, 1000);
    rootItem->setParentItem(window.contentItem());
    window.show();
    auto* const page = visualItem(rootItem, QStringLiteral("motorPage"));
    auto* const sliders = visualItem(rootItem, QStringLiteral("subMotor_sliders"));
    auto* const slider = visualItem(rootItem, QStringLiteral("subMotor_slider_0"));
    auto* const safetySwitch = visualItem(rootItem, QStringLiteral("subMotor_safetySwitch"));
    auto* const cooldown = root->findChild<QObject*>(QStringLiteral("subMotor_coolDownTimer"));
    auto* const commands = root->findChild<QObject*>(QStringLiteral("subMotor_commandTimer"));
    QVERIFY(page && sliders && slider && safetySwitch && cooldown && commands);
    const auto state = [&]() {
        return QStringLiteral(
                   "armed=%1 canRun=%2 intent=%3 pageVisible=%4 slidersEnabled=%5 cooldown=%6 mode=%7 detect=%8")
            .arg(_vehicle->armed())
            .arg(page->property("canRunManualTest").toBool())
            .arg(page->property("shouldRunManualTest").toBool())
            .arg(page->isVisible())
            .arg(sliders->isEnabled())
            .arg(cooldown->property("running").toBool())
            .arg(_vehicle->flightMode(), _vehicle->motorDetectionFlightMode());
    };
    QCOMPARE(cooldown->property("interval").toInt(), 11000);

    if (overrideEnabled && initiallyArmed) {
        // No new armedChanged signal is emitted merely because the page was opened.
        QVERIFY(_vehicle->armed());
        QVERIFY(safetySwitch->property("checked").toBool());
        QVERIFY2(sliders->isEnabled(), qPrintable(state()));
    } else {
        QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->armed(), 5000);
        QVERIFY(!sliders->isEnabled());
        QVERIFY(!commands->property("running").toBool());
        if (cooldown->property("running").toBool()) {
            // Advance only this test's QML timer after checking the production duration.
            QVERIFY(cooldown->setProperty("interval", 1));
            QTRY_VERIFY_WITH_TIMEOUT(!cooldown->property("running").toBool(), 1000);
            QVERIFY(cooldown->setProperty("interval", 11000));
        }
        QVERIFY(safetySwitch->isEnabled());
        QVERIFY(safetySwitch->setProperty("checked", true));
        QVERIFY(QMetaObject::invokeMethod(safetySwitch, "toggled"));
        // Sending an arm request alone must not enable outputs before the heartbeat confirms it.
        QVERIFY(!sliders->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(sliders->isEnabled(), 1000);
    }
    QCOMPARE(slider->property("value").toInt(), 50);
    QTRY_VERIFY_WITH_TIMEOUT(_mockLink->receivedMavCommandCount(MAV_CMD_DO_MOTOR_TEST) > 0, 3000);
    QVERIFY(!motorPercents.isEmpty());
    for (const float percent : motorPercents) {
        QCOMPARE(percent, 50.0f);
    }

    // Toggling the global setting does not strand an already usable motor page.
    setting->setRawValue(!overrideEnabled);
    QVERIFY2(sliders->isEnabled(), qPrintable(state()));
    setting->setRawValue(overrideEnabled);
    QVERIFY2(sliders->isEnabled(), qPrintable(state()));

    // Hidden pages stop their timer and reset an old slider before becoming visible again.
    QVERIFY(slider->setProperty("value", 65));
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(motorPercents.last() - 50.0f) == 15.0f, 3000);
    rootItem->setVisible(false);
    QVERIFY(!commands->property("running").toBool());
    QCOMPARE(slider->property("value").toInt(), 50);
    motorPercents.clear();
    rootItem->setVisible(true);
    QVERIFY2(sliders->isEnabled(), qPrintable(state()));
    QTRY_VERIFY_WITH_TIMEOUT(!motorPercents.isEmpty(), 3000);
    for (const float percent : motorPercents) {
        QCOMPARE(percent, 50.0f);
    }

    // Stopping a test blocks commands immediately, before the disarm reply arrives.
    QVERIFY(safetySwitch->setProperty("checked", false));
    QVERIFY(QMetaObject::invokeMethod(safetySwitch, "toggled"));
    QVERIFY(!sliders->isEnabled());
    QVERIFY(!commands->property("running").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!_vehicle->armed(), 5000);
    QVERIFY(cooldown->property("running").toBool());
    QVERIFY(!safetySwitch->isEnabled());

    // An external re-arm during cooldown must not bypass the timer interlock.
    _vehicle->setArmed(true, false);
    QTRY_VERIFY_WITH_TIMEOUT(_vehicle->armed(), 5000);
    QVERIFY(cooldown->property("running").toBool());
    QVERIFY(!sliders->isEnabled());
    QVERIFY(!commands->property("running").toBool());
    QVERIFY(cooldown->setProperty("interval", 1));
    QTRY_VERIFY_WITH_TIMEOUT(!cooldown->property("running").toBool(), 1000);
    QVERIFY(safetySwitch->isEnabled());
    QVERIFY2(sliders->isEnabled(), qPrintable(state()));
    QCOMPARE(slider->property("value").toInt(), 50);

    // The existing automatic direction-detection exclusion remains effective.
    _vehicle->setFlightMode(_vehicle->motorDetectionFlightMode());
    QTRY_COMPARE_WITH_TIMEOUT(_vehicle->flightMode(), _vehicle->motorDetectionFlightMode(), 5000);
    QVERIFY(!sliders->isEnabled());
    QVERIFY(!commands->property("running").toBool());
}

UT_REGISTER_TEST(APMSubMotorTest, TestLabel::Integration)
