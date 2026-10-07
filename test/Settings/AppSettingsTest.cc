#include "AppSettingsTest.h"

#include <QtCore/QScopeGuard>
#include <QtCore/QTranslator>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtTest/QSignalSpy>
#include <memory>

#include "AppSettings.h"
#include "FirmwarePluginManager.h"
#include "QGCCorePlugin.h"
#include "QGCMAVLink.h"
#include "QGCPalette.h"
#include "SettingsManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(AppSettingsTest, TestLabel::Unit)

void AppSettingsTest::_preferredFirmwareClassEnumFiltered()
{
    _verifyFirmwareClassEnumFiltered(SettingsManager::instance()->appSettings()->preferredFirmwareClass());
}

void AppSettingsTest::_offlineEditingFirmwareClassEnumFiltered()
{
    _verifyFirmwareClassEnumFiltered(SettingsManager::instance()->appSettings()->offlineEditingFirmwareClass());
}

void AppSettingsTest::_chineseTranslations_data()
{
    QTest::addColumn<QString>("catalog");
    QTest::addColumn<QByteArray>("context");
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<QString>("expected");

    QTest::newRow("video-source") << QStringLiteral("json") << QByteArray("Video.SettingsUI.json")
                                  << QByteArray("Video Source") << QStringLiteral("视频源");
    QTest::newRow("units") << QStringLiteral("json") << QByteArray("General.SettingsUI.json") << QByteArray("Units")
                           << QStringLiteral("单位");
    QTest::newRow("guided-commands") << QStringLiteral("json") << QByteArray("FlyView.SettingsUI.json")
                                     << QByteArray("Guided Commands") << QStringLiteral("引导命令");
    QTest::newRow("decoder") << QStringLiteral("json") << QByteArray("Video.SettingsUI.json")
                             << QByteArray("DeepShark Decoder") << QStringLiteral("DeepShark 解码器");
    QTest::newRow("decoder-label") << QStringLiteral("json") << QByteArray("Video.SettingsGroup.json")
                                   << QByteArray("Force video decoder priority") << QStringLiteral("视频解码优先级");
    QTest::newRow("ai") << QStringLiteral("json") << QByteArray("Video.SettingsUI.json") << QByteArray("AI Detection")
                        << QStringLiteral("AI 检测");
    QTest::newRow("video-alerts") << QStringLiteral("json") << QByteArray("Video.SettingsUI.json")
                                  << QByteArray("Connection Alerts") << QStringLiteral("连接告警");
    QTest::newRow("link-alerts") << QStringLiteral("json") << QByteArray("CommLinks.SettingsUI.json")
                                 << QByteArray("Connection Alerts") << QStringLiteral("连接告警");
    QTest::newRow("joystick-repeat") << QStringLiteral("source") << QByteArray("JoystickComponentButtons")
                                     << QByteArray("Repeat") << QStringLiteral("重复");
    QTest::newRow("connect") << QStringLiteral("source") << QByteArray("LinkConfigurationManager")
                             << QByteArray("Connect") << QStringLiteral("连接");
    QTest::newRow("failsafe") << QStringLiteral("source") << QByteArray("APMFlightSafetyComponentSub")
                              << QByteArray("Failsafe Actions") << QStringLiteral("故障保护动作");
    QTest::newRow("cancel-roi") << QStringLiteral("source") << QByteArray("PlanView") << QByteArray("Cancel ROI")
                                << QStringLiteral("取消ROI");
    QTest::newRow("setup-safety-override")
        << QStringLiteral("json") << QByteArray("App.SettingsGroup.json") << QByteArray("Disable safety restrictions")
        << QStringLiteral("禁用安全限制");
    QTest::newRow("setup-safety-description")
        << QStringLiteral("json") << QByteArray("App.SettingsGroup.json")
        << QByteArray(
               "Allow ArduSub configuration editing while armed. Flight-controller failsafes and calibration checks "
               "remain enabled.")
        << QStringLiteral("允许 ArduSub 在解锁状态下编辑配置。飞控故障保护和校准检查仍然有效。");
}

void AppSettingsTest::_chineseTranslations()
{
    QFETCH(QString, catalog);
    QFETCH(QByteArray, context);
    QFETCH(QByteArray, source);
    QFETCH(QString, expected);

    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/qgc_%1_zh_CN.qm").arg(catalog)));
    QCOMPARE(translator.translate(context.constData(), source.constData()), expected);
}

void AppSettingsTest::_setupSafetyOverride_data()
{
    QTest::addColumn<int>("firmware");
    QTest::addColumn<int>("vehicleType");
    QTest::addColumn<bool>("supported");
    QTest::newRow("ardusub") << int(MAV_AUTOPILOT_ARDUPILOTMEGA) << int(MAV_TYPE_SUBMARINE) << true;
    QTest::newRow("arducopter") << int(MAV_AUTOPILOT_ARDUPILOTMEGA) << int(MAV_TYPE_QUADROTOR) << false;
    QTest::newRow("px4") << int(MAV_AUTOPILOT_PX4) << int(MAV_TYPE_QUADROTOR) << false;
    QTest::newRow("generic-sub") << int(MAV_AUTOPILOT_GENERIC) << int(MAV_TYPE_SUBMARINE) << false;
}

void AppSettingsTest::_setupSafetyOverride()
{
    QFETCH(int, firmware);
    QFETCH(int, vehicleType);
    QFETCH(bool, supported);
    auto* const setting = SettingsManager::instance()->appSettings()->disableSetupSafetyRestrictions();
    const QVariant previousValue = setting->rawValue();
    const auto restoreSetting = qScopeGuard([&]() { setting->setRawValue(previousValue); });
    QCOMPARE(setting->metaData()->rawDefaultValue().toBool(), false);
    setting->setRawValue(false);
    Vehicle vehicle(static_cast<MAV_AUTOPILOT>(firmware), static_cast<MAV_TYPE>(vehicleType));
    QQmlEngine::setObjectOwnership(&vehicle, QQmlEngine::CppOwnership);
    QSignalSpy overrideSpy(&vehicle, &Vehicle::setupSafetyRestrictionsDisabledChanged);

    QGCCorePlugin::instance()->init();
    QGCPalette palette;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("qgcPal"), &palette);
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QGroundControl
import QGroundControl.Controls
Item {
    property var fixtureVehicle
    property bool armedState: false
    property bool flyingState: false
    property QtObject globals: QtObject {
        property QtObject activeVehicle: QtObject {
            property bool armed: armedState
            property bool flying: flyingState
            property bool rover: false
            property bool setupSafetyRestrictionsDisabled: fixtureVehicle.setupSafetyRestrictionsDisabled
        }
    }
    property QtObject vehicleComponent: QtObject {
        property string name: "Configuration fixture"
        property string description: "Configuration fixture"
        property bool allowSetupWhileArmed: false
        property bool allowSetupWhileFlying: false
    }
    SetupPage {
        objectName: "setupPage"
        width: 800
        height: 600
        pageComponent: Component { Item { width: 600; height: 300 } }
    }
})",
                      QUrl(QStringLiteral("qrc:/setup-safety-fixture.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(
        component.createWithInitialProperties({{QStringLiteral("fixtureVehicle"), QVariant::fromValue(&vehicle)}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* const page = root->findChild<QObject*>(QStringLiteral("setupPage"));
    QVERIFY(page);
    for (const bool armed : {false, true}) {
        for (const bool flying : {false, true}) {
            root->setProperty("armedState", armed);
            root->setProperty("flyingState", flying);
            setting->setRawValue(false);
            QCOMPARE(vehicle.setupSafetyRestrictionsDisabled(), false);
            QCOMPARE(page->property("enabled").toBool(), !armed && !flying);
            setting->setRawValue(true);
            QCOMPARE(vehicle.setupSafetyRestrictionsDisabled(), supported);
            QCOMPARE(page->property("enabled").toBool(), supported || (!armed && !flying));
            setting->setRawValue(false);
            QCOMPARE(page->property("enabled").toBool(), !armed && !flying);
        }
    }
    QVERIFY(overrideSpy.count() >= 8);
    if (supported) {
        QQmlComponent settingsComponent(
            &engine, QUrl(QStringLiteral("qrc:/qml/QGroundControl/AppSettings/GeneralSettings.qml")));
        QVERIFY2(settingsComponent.isReady(), qPrintable(settingsComponent.errorString()));
        std::unique_ptr<QObject> settingsPage(settingsComponent.create());
        QVERIFY2(settingsPage, qPrintable(settingsComponent.errorString()));
        auto* const toggle =
            settingsPage->findChild<QObject*>(QStringLiteral("settingsCheckBox_disableSetupSafetyRestrictions"));
        QVERIFY(toggle);
        QCOMPARE(qvariant_cast<Fact*>(toggle->property("fact")), setting);
        QVERIFY(!toggle->property("checked").toBool());
        QVERIFY(toggle->setProperty("checked", true));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(setting->rawValue().toBool());
        QVERIFY(vehicle.setupSafetyRestrictionsDisabled());
        QVERIFY(page->property("enabled").toBool());
        QVERIFY(toggle->setProperty("checked", false));
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(!setting->rawValue().toBool());
        QVERIFY(!page->property("enabled").toBool());
    }
}

void AppSettingsTest::_verifyFirmwareClassEnumFiltered(Fact *fact)
{
    const QList<QGCMAVLink::FirmwareClass_t> supportedClasses = FirmwarePluginManager::instance()->supportedFirmwareClasses();

    const QVariantList enumValues = fact->enumValues();
    QCOMPARE(fact->enumStrings().count(), enumValues.count());
    QVERIFY(!enumValues.isEmpty());

    for (const QVariant &enumValue : enumValues) {
        const auto firmwareClass = static_cast<QGCMAVLink::FirmwareClass_t>(enumValue.toUInt());
        QVERIFY2(supportedClasses.contains(firmwareClass),
                 qPrintable(QStringLiteral("%1 enum offers unsupported firmware class %2").arg(fact->name()).arg(enumValue.toUInt())));
    }

    QVERIFY2(supportedClasses.contains(static_cast<QGCMAVLink::FirmwareClass_t>(fact->rawValue().toUInt())),
             qPrintable(QStringLiteral("%1 value is an unsupported firmware class %2").arg(fact->name()).arg(fact->rawValue().toUInt())));
}
