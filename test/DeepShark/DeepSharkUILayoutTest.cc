#include "DeepSharkUILayoutTest.h"

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QTranslator>
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>
#include <memory>

#include "AutoPilotPlugin.h"
#include "DeepSharkVideoController.h"
#include "Fact.h"
#include "Joystick.h"
#include "JoystickManager.h"
#include "JoystickManagerSettings.h"
#include "MockJoystick.h"
#include "MockLink.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "SettingsManager.h"
#include "Vehicle.h"

void DeepSharkUILayoutTest::_toolbarAndJoystickLayout()
{
    ignoreLogMessage("qt.qml.propertyCache.append", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Member enabled of the object QQuickPinchArea overrides")));
    ignoreLogMessage("API.QGCApplication.AppMessage", QtDebugMsg,
                     QRegularExpression(QStringLiteral("showAppMessage:.*Configuration tasks remain")));
    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }
    _window->setGeometry(_window->screen()->availableGeometry().adjusted(24, 48, -24, -48));
    auto* const link = MockLink::startAPMArduSubMockLink();
    QVERIFY(link);
    auto* const vehicles = MultiVehicleManager::instance();
    const auto disconnect = qScopeGuard([&]() { link->disconnect(); });
    QTRY_VERIFY_WITH_TIMEOUT(vehicles->activeVehicle(), 10000);
    auto* const vehicle = vehicles->activeVehicle();
    QTRY_VERIFY_WITH_TIMEOUT(vehicle->parameterManager()->parametersReady(), 10000);
    auto* const mode = findVisibleItem(_rootItem, QStringLiteral("toolbar_flightModeIndicator"), 5000);
    auto* const arm = findVisibleItem(_rootItem, QStringLiteral("toolbar_armDisarmButton"), 5000);
    auto* const heartbeat = findVisibleItem(_rootItem, QStringLiteral("toolbarConnectionAlert"), 5000);
    QVERIFY(mode && arm && heartbeat);
    QTRY_VERIFY_WITH_TIMEOUT(arm->isEnabled(), 5000);
    QVERIFY(mode->mapToScene(QPointF(mode->width(), 0)).x() <= arm->mapToScene(QPointF()).x());
    QVERIFY(arm->mapToScene(QPointF(arm->width(), 0)).x() <= heartbeat->mapToScene(QPointF()).x());
    QVERIFY(_window->grabWindow().save(QDir::current().filePath(QStringLiteral("toolbar-arm-heartbeat.png"))));
    const qreal armWidth = arm->width();
    QVERIFY(QMetaObject::invokeMethod(arm, "activated"));
    QTRY_VERIFY_WITH_TIMEOUT(vehicle->armed(), 5000);
    QCOMPARE(arm->width(), armWidth);
    QVERIFY(QMetaObject::invokeMethod(arm, "activated"));
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle->armed(), 5000);

    auto mock = std::unique_ptr<MockJoystick>(MockJoystick::create(QStringLiteral("Joystick UI fixture"), 6, 8, 0));
    QVERIFY(mock && mock->isValid());
    auto* const manager = JoystickManager::instance();
    manager->init();
    SettingsManager::instance()->joystickManagerSettings()->activeJoystickName()->setRawValue(
        QStringLiteral("Joystick UI fixture"));
    QPointer<Joystick> joystick = manager->activeJoystick();
    QVERIFY(joystick);
    const auto releaseJoystick = qScopeGuard([&]() {
        manager->setActiveJoystickEnabledForActiveVehicle(false);
        if (joystick)
            joystick->stop();
        mock.reset();
        manager->init();
    });
    QVERIFY(QMetaObject::invokeMethod(_window, "showKnownVehicleComponentConfigPage",
                                      Q_ARG(QVariant, QVariant(AutoPilotPlugin::KnownJoystickVehicleComponent))));
    QPointer<QQuickItem> tabs = findVisibleItem(_rootItem, QStringLiteral("joystickConfigurationTabs"), 5000);
    QVERIFY(tabs);
    QCOMPARE(tabs->property("currentIndex").toInt(), 2);
    auto* const loader = findVisibleItem(_rootItem, QStringLiteral("joystickConfigurationLoader"), 5000);
    QVERIFY(loader);
    QPointer<QObject> configuration = qvariant_cast<QObject*>(loader->property("item"));
    QVERIFY(configuration);
    auto* const controller = configuration->findChild<QObject*>(QStringLiteral("joystickPageController"));
    QVERIFY(controller);
    QTRY_VERIFY_WITH_TIMEOUT(controller->property("channelCount").toInt() >= 4, 5000);
    QVERIFY(clickButton(QStringLiteral("remoteControlCalibrationNext")));
    QTRY_VERIFY_WITH_TIMEOUT(tabs && !tabs->isEnabled(), 5000);
    QVERIFY(clickButton(QStringLiteral("remoteControlCalibrationCancel")));
    QTRY_VERIFY_WITH_TIMEOUT(tabs && tabs->isEnabled(), 5000);
    for (const auto& tab : {QStringLiteral("joystickGeneralTab"), QStringLiteral("joystickButtonsTab"),
                            QStringLiteral("joystickCalibrationTab"), QStringLiteral("joystickAdvancedTab")}) {
        QVERIFY(clickButton(tab));
        QCOMPARE(qvariant_cast<QObject*>(loader->property("item")), configuration.data());
        QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
        const QImage screenshot = _window->grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(QDir::current().filePath(tab + QStringLiteral(".png"))));
    }
    QVERIFY(clickButton(QStringLiteral("joystickButtonsTab")));
    auto* const row = findVisibleItem(_rootItem, QStringLiteral("joystickButtonRow3"), 5000);
    QVERIFY(row);
    QVERIFY(mock->pressButton(3));
    QTRY_VERIFY_WITH_TIMEOUT(row->property("pressed").toBool(), 5000);
    QVERIFY(mock->releaseButton(3));
    QTRY_VERIFY_WITH_TIMEOUT(!row->property("pressed").toBool(), 5000);
    // Unplugging the selected device must remove its controller without stale bindings.
    ignoreLogMessage("Joystick.JoystickSDL", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Joystick disconnected during update:.*Joystick UI fixture")));
    ignoreLogMessage(
        "Joystick.Joystick", QtWarningMsg,
        QRegularExpression(QStringLiteral("Joystick disconnected or update failed:.*Joystick UI fixture")));
    // The existing manager can restart configuration polling during its device-removal rescan.
    ignoreLogMessage("Joystick.Joystick", QtWarningMsg,
                     QRegularExpression(QStringLiteral(
                         "^Joystick polling thread not running but configuration polling flag set. Forcing start.$")));
    ignoreLogMessage(
        "Joystick.JoystickSDL", QtWarningMsg,
        QRegularExpression(QStringLiteral("SDL_OpenGamepad failed: Couldn't find mapping for device \\(\\d+\\)")));
    ignoreLogMessage("Joystick.Joystick", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Failed to open joystick:.*Joystick UI fixture")));
    mock.reset();
    manager->init();
    QTRY_VERIFY_WITH_TIMEOUT(!configuration, 5000);
    QVERIFY(findVisibleItem(_rootItem, QStringLiteral("joystickDeviceCombo"), 5000));
}

void DeepSharkUILayoutTest::_customWindowCanCloseAndReopen()
{
    // Qt 6.11 warns about its own legacy PinchArea property when the stock map loads.
    ignoreLogMessage("qt.qml.propertyCache.append", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Member enabled of the object QQuickPinchArea overrides")));
    for (int cycle = 0; cycle < 2; ++cycle) {
        startUI();
        if (QTest::currentTestFailed()) {
            return;
        }
        _window->setGeometry(_window->screen()->availableGeometry().adjusted(32, 64, -32, -64));
        auto* panel = findVisibleItem(_rootItem, QStringLiteral("deepSharkFourVideoPanel"), 5000);
        QVERIFY2(panel, "The complete MainWindow must instantiate the custom four-video panel");
        QVERIFY(findItem(_rootItem, QStringLiteral("deepSharkCustomLayer")));
        QVERIFY(findItem(_rootItem, QStringLiteral("toolbarConnectionAlert")));
        QCOMPARE(panel->findChildren<DeepSharkVideoController*>().size(), 4);

        QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
        const QImage screenshot = _window->grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(QDir::current().filePath(QStringLiteral("deepshark-debug-ui-%1.png").arg(cycle + 1))));

        closeUIWindow();
        QVERIFY(!_window->isVisible());
        destroyUIEngine();
        QVERIFY(!_engine);
        QVERIFY(!_window);
    }
}

void DeepSharkUILayoutTest::_chineseSettingsPages()
{
    ignoreLogMessage("qt.qml.propertyCache.append", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Member enabled of the object QQuickPinchArea overrides")));
    QTranslator sourceTranslator;
    QTranslator jsonTranslator;
    QVERIFY(sourceTranslator.load(QStringLiteral(":/i18n/qgc_source_zh_CN.qm")));
    QVERIFY(jsonTranslator.load(QStringLiteral(":/i18n/qgc_json_zh_CN.qm")));
    QCoreApplication::installTranslator(&sourceTranslator);
    QCoreApplication::installTranslator(&jsonTranslator);
    const auto restoreTranslators = qScopeGuard([&]() {
        QCoreApplication::removeTranslator(&jsonTranslator);
        QCoreApplication::removeTranslator(&sourceTranslator);
    });
    // These pages are generated with qsTranslate() using the JSON filename as context.
    QCOMPARE(QCoreApplication::translate("APMPower.VehicleConfig.json", "Battery monitor"),
             QStringLiteral("电池监测器"));
    QCOMPARE(QCoreApplication::translate("APMFailsafes.VehicleConfig.json", "Ground Station Failsafe"),
             QStringLiteral("地面站失效保护"));
    QCOMPARE(QCoreApplication::translate("APMLogging.VehicleConfig.json", "Logged data groups"),
             QStringLiteral("记录的数据组"));
    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }
    _window->setGeometry(_window->screen()->availableGeometry().adjusted(32, 64, -32, -64));
    QVERIFY(QMetaObject::invokeMethod(_window, "showSettingsTool", Q_ARG(QVariant, QVariant(QStringLiteral("Video")))));
    QQuickItem* page = findVisibleItem(_rootItem, QStringLiteral("settingsPage_Video"), 5000);
    QVERIFY(page);
    QQuickItem* source = findItem(page, QStringLiteral("settingsGroup_VideoSource"));
    QQuickItem* decoder = findItem(page, QStringLiteral("settingsGroup_DeepSharkDecoder"));
    QVERIFY(source);
    QVERIFY(decoder);
    QCOMPARE(source->property("heading").toString(), QStringLiteral("视频源"));
    QCOMPARE(decoder->property("heading").toString(), QStringLiteral("DeepShark 解码器"));
    QVERIFY(scrollIntoView(decoder, QStringLiteral("settingsPageFlickable")));
    const QImage screenshot = _window->grabWindow();
    QVERIFY(!screenshot.isNull());
    QVERIFY(screenshot.save(QDir::current().filePath(QStringLiteral("deepshark-chinese-video-settings.png"))));
    // Exercise the two pages where users reported untranslated labels and help text.
    const auto verifySettingsPage = [&](const QString& name, const QString& objectName, const QString& helpText,
                                        const QString& screenshotName) {
        QVERIFY(QMetaObject::invokeMethod(_window, "showSettingsTool", Q_ARG(QVariant, QVariant(name))));
        QQuickItem* settingsPage = findVisibleItem(_rootItem, objectName, 5000);
        QVERIFY(settingsPage);
        const auto hasHelpText = [&]() {
            for (const QObject* child : settingsPage->findChildren<QObject*>()) {
                if (child->property("text").toString() == helpText) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(hasHelpText(), 5000);
        QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
        const QImage pageScreenshot = _window->grabWindow();
        QVERIFY(!pageScreenshot.isNull());
        QVERIFY(pageScreenshot.save(QDir::current().filePath(screenshotName)));
    };
    verifySettingsPage(QStringLiteral("General"), QStringLiteral("settingsPage_General"),
                       QStringLiteral("控制声音警报和通知的音量。"),
                       QStringLiteral("deepshark-chinese-general-settings.png"));
    verifySettingsPage(QStringLiteral("Fly View"), QStringLiteral("settingsPage_FlyView"),
                       QStringLiteral("根据地面站 GPS 位置，自动更新载具的返航点。"),
                       QStringLiteral("deepshark-chinese-fly-settings.png"));
    // The current metadata includes an ArduPilot qualifier; an older catalog key
    // without it silently falls back to English despite having a finished translation.
    verifySettingsPage(QStringLiteral("Fly View"), QStringLiteral("settingsPage_FlyView"),
                       QStringLiteral("固定翼飞行时，围绕前往目标点盘旋的半径。（仅适用于 ArduPilot）"),
                       QStringLiteral("deepshark-chinese-fly-settings.png"));
    stopUI();
}

UT_REGISTER_TEST(DeepSharkUILayoutTest, TestLabel::Integration)
