#include "DeepSharkUILayoutTest.h"

#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QTranslator>
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QSignalSpy>

#include "DeepSharkVideoController.h"

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
