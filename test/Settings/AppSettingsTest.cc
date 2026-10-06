#include "AppSettingsTest.h"

#include <QtCore/QTranslator>

#include "AppSettings.h"
#include "FirmwarePluginManager.h"
#include "QGCMAVLink.h"
#include "SettingsManager.h"

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
