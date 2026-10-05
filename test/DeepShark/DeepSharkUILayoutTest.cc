#include "DeepSharkUILayoutTest.h"

#include <QtCore/QDir>
#include <QtCore/QRegularExpression>
#include <QtGui/QImage>
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
        _window->resize(1920, 1080);
        auto* panel = findVisibleItem(_rootItem, QStringLiteral("deepSharkFourVideoPanel"), 5000);
        QVERIFY2(panel, "The complete MainWindow must instantiate the custom four-video panel");
        QVERIFY(findItem(_rootItem, QStringLiteral("deepSharkCustomLayer")));
        QVERIFY(findItem(_rootItem, QStringLiteral("toolbarConnectionAlert")));
        QCOMPARE(panel->findChildren<DeepSharkVideoController*>().size(), 4);

        QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
        _window->requestUpdate();
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

UT_REGISTER_TEST(DeepSharkUILayoutTest, TestLabel::Integration)
