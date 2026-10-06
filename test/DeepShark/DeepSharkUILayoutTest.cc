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
    stopUI();
}

UT_REGISTER_TEST(DeepSharkUILayoutTest, TestLabel::Integration)
