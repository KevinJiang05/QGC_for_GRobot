#include "DeepSharkVideoControllerTest.h"

#include "DeepSharkVideoController.h"
#include "QGCCorePlugin.h"
#include "VideoReceiver.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <memory>

#ifdef QGC_GST_STREAMING
#include <gst/gstelement.h>
#endif

void DeepSharkVideoControllerTest::_pendingAutoStartIsCancelled()
{
    DeepSharkVideoController controller;
    controller.setUri(QStringLiteral("rtsp://127.0.0.1/test"));
    controller.setAutoStart(true);
    controller.setAutoStart(false);

    QTest::qWait(350);

    QCOMPARE(controller.startAttempts(), 0);
    QVERIFY(!controller.autoStart());
}

void DeepSharkVideoControllerTest::_clearingUriCancelsPendingAutoStart()
{
    DeepSharkVideoController controller;
    controller.setUri(QStringLiteral("rtsp://127.0.0.1/test"));
    controller.setAutoStart(true);
    controller.setUri(QString());

    QTest::qWait(350);

    QCOMPARE(controller.startAttempts(), 0);
}

void DeepSharkVideoControllerTest::_failureSignalIsIndependentFromDisplayText()
{
    DeepSharkVideoController controller;
    QSignalSpy failureSpy(&controller, &DeepSharkVideoController::failure);

    controller.setUri(QStringLiteral("rtsp://127.0.0.1/test"));
    controller.setAutoStart(true);
    controller.start();

    QCOMPARE(failureSpy.count(), 1);
    QVERIFY(!failureSpy.constFirst().constFirst().toString().isEmpty());
    QCOMPARE(controller.statusText(), failureSpy.constFirst().constFirst().toString());
}

void DeepSharkVideoControllerTest::_restartRebuildsVideoSink()
{
#ifndef QGC_GST_STREAMING
    QSKIP("GStreamer video sinks are not enabled");
#else
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));

    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/VideoTile.qml")),
                            QQmlComponent::PreferSynchronous);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    const QVariantMap initialProperties{
        {QStringLiteral("videoEnabled"), true},
        {QStringLiteral("videoSource"), QStringLiteral("rtsp://127.0.0.1/test")},
        {QStringLiteral("receiverName"), QStringLiteral("deepSharkVideoRestartTest")},
    };
    std::unique_ptr<QObject> tile(component.createWithInitialProperties(initialProperties));
    QVERIFY2(tile, qPrintable(component.errorString()));

    DeepSharkVideoController *const controller = tile->findChild<DeepSharkVideoController *>();
    QVERIFY(controller);
    QVERIFY(controller->_sink);

    GstElement *const previousSink = GST_ELEMENT(controller->_sink);
    gst_object_ref(previousSink);

    controller->restart(30000);

    QVERIFY(controller->_sink);
    QVERIFY(controller->_sink != previousSink);
    QCOMPARE(controller->_receiver->sink(), controller->_sink);

    tile->setProperty("videoEnabled", false);
    gst_object_unref(previousSink);
#endif
}

void DeepSharkVideoControllerTest::_failedRestartCanScheduleAgain()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));

    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/VideoTile.qml")),
                            QQmlComponent::PreferSynchronous);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    std::unique_ptr<QObject> tile(component.create());
    QVERIFY2(tile, qPrintable(component.errorString()));
    tile->setProperty("reconnectPending", true);

    DeepSharkVideoController *const controller = tile->findChild<DeepSharkVideoController *>();
    QVERIFY(controller);
    QVERIFY(QMetaObject::invokeMethod(controller,
                                      "failure",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("test failure"))));
    QCOMPARE(tile->property("reconnectPending").toBool(), false);
}

void DeepSharkVideoControllerTest::_startedRestartCanScheduleAgain()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));

    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/VideoTile.qml")),
                            QQmlComponent::PreferSynchronous);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));

    std::unique_ptr<QObject> tile(component.create());
    QVERIFY2(tile, qPrintable(component.errorString()));
    tile->setProperty("reconnectPending", true);

    DeepSharkVideoController *const controller = tile->findChild<DeepSharkVideoController *>();
    QVERIFY(controller);
    QVERIFY(QMetaObject::invokeMethod(controller, "startAttemptsChanged", Qt::DirectConnection));
    QCOMPARE(tile->property("reconnectPending").toBool(), false);
}

void DeepSharkVideoControllerTest::_flyViewStatusQmlLoads()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));

    const QList<QUrl> urls{
        QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/Attitude3DPanel.qml")),
        QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/DeepSharkStatusPanel.qml")),
        QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/FourVideoPanel.qml")),
        QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/VideoTile.qml")),
        QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/FlyViewCustomLayer.qml")),
    };
    for (const QUrl &url : urls) {
        QQmlComponent component(&engine, url, QQmlComponent::PreferSynchronous);
        QStringList errors;
        for (const QQmlError &error : component.errors()) {
            errors.append(error.toString());
        }
        QVERIFY2(component.status() == QQmlComponent::Ready,
                 qPrintable(url.toString() + QLatin1Char('\n') + errors.join(QLatin1Char('\n'))));
    }
}
