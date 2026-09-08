#include "DeepSharkVideoControllerTest.h"

#include "DeepSharkVideoController.h"
#include "QGCCorePlugin.h"
#include "QGCOptions.h"
#include "VideoReceiver.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/QJSValue>
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
        QUrl(QStringLiteral("qrc:/qml/QGroundControl/Controls/FlyViewToolBar.qml")),
        QUrl(QStringLiteral("qrc:/qml/QGroundControl/AppSettings/VideoSettings.qml")),
        QUrl(QStringLiteral("qrc:/qml/QGroundControl/AppSettings/LinkSettings.qml")),
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

void DeepSharkVideoControllerTest::_videoRowsUpdateWithoutReplacingDelegates()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine,
                            QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/FourVideoPanel.qml")),
                            QQmlComponent::PreferSynchronous);
    std::unique_ptr<QObject> panel(component.create());
    QVERIFY2(panel, qPrintable(component.errorString()));
    const QJSValue rows = panel->property("videoRows").value<QJSValue>();
    QCOMPARE(rows.property(QStringLiteral("length")).toInt(), 4);
    QObject *const row = rows.property(0).toQObject();
    QVERIFY(row);
    QObject *const tile = row->property("tile").value<QObject *>();
    QVERIFY(tile);
    auto *controller = tile->findChild<DeepSharkVideoController *>();
    QVERIFY(controller);
    QSignalSpy rowsChanged(panel.get(), SIGNAL(videoRowsChanged()));
    QQmlComponent statusComponent(&engine,
                                  QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/DeepSharkStatusPanel.qml")),
                                  QQmlComponent::PreferSynchronous);
    std::unique_ptr<QObject> statusPanel(statusComponent.createWithInitialProperties(
        {{QStringLiteral("videoRows"), panel->property("videoRows")}}));
    QVERIFY2(statusPanel, qPrintable(statusComponent.errorString()));
    QObject *videoRepeater = nullptr;
    for (auto *child : statusPanel->findChildren<QObject *>()) {
        if (child->inherits("QQuickRepeater") && child->property("count").toInt() == 4) {
            videoRepeater = child;
            break;
        }
    }
    QVERIFY(videoRepeater);
    QSignalSpy added(videoRepeater, SIGNAL(itemAdded(int,QQuickItem*)));
    QSignalSpy removed(videoRepeater, SIGNAL(itemRemoved(int,QQuickItem*)));
    QVERIFY(added.isValid());
    QVERIFY(removed.isValid());
    controller->_setDecoding(true);
    for (int sample = 0; sample < 100; ++sample) {
        controller->_setFrameRate(15.0 + sample % 10);
        QCOMPARE(row->property("fps").toString(), controller->frameRateText());
        QCoreApplication::processEvents();
        if (sample % 10 == 0) {
            engine.collectGarbage();
        }
    }
    QCOMPARE(rowsChanged.count(), 0);
    QCOMPARE(added.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(panel->property("videoRows").value<QJSValue>().property(0).toQObject(), row);
}

void DeepSharkVideoControllerTest::_disabledVideoResourcesCanBeRecreated()
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
    std::unique_ptr<QObject> tile(component.create());
    QVERIFY2(tile, qPrintable(component.errorString()));
    auto *controller = tile->findChild<DeepSharkVideoController *>();
    QVERIFY(controller);
    QVERIFY(!controller->_receiver);
    QVERIFY(!controller->_sink);
    QVERIFY(!controller->_frameRateTimer.isActive());
    tile->setProperty("videoEnabled", true);
    QVERIFY(controller->_receiver);
    QVERIFY(controller->_sink);
    QPointer<VideoReceiver> retiredReceiver = controller->_receiver;
    // Clear the output while start is pending: a queued decoder may still use
    // the sink, so it must remain alive until completion reports failure/stop.
    controller->_startPending = true;
    tile->setProperty("videoEnabled", false);
    QVERIFY(controller->_sink);
    void *const pendingSink = controller->_sink;
    tile->setProperty("videoEnabled", true);
    QCOMPARE(controller->_sink, pendingSink);
    tile->setProperty("videoEnabled", false);
    emit retiredReceiver->onStartComplete(VideoReceiver::STATUS_FAIL);
    QVERIFY(!controller->_receiver);
    QVERIFY(!controller->_sink);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(retiredReceiver.isNull());

    tile->setProperty("videoEnabled", true);
    controller->setUri(QStringLiteral("rtsp://127.0.0.1:1/test"));
    controller->setAutoStart(true);
    controller->start();
    QCOMPARE(controller->startAttempts(), 1);
    QVERIFY(controller->_frameRateTimer.isActive());
    controller->setAutoStart(false);
    // Re-enable before the worker has completed the preceding operation.
    controller->setAutoStart(true);
    QVERIFY(controller->_restartRequested);
    controller->setAutoStart(false);
    QTRY_VERIFY_WITH_TIMEOUT(!controller->_receiver, 10000);
    QVERIFY(!controller->_sink);
    QVERIFY(!controller->_frameRateTimer.isActive());
#endif
}

void DeepSharkVideoControllerTest::_coreOptionsSurviveGarbageCollection()
{
    QObject owner;
    QGCCorePlugin plugin(&owner);
    // The default fly-view options currently have no QObject parent.
    std::unique_ptr<QGCFlyViewOptions> flyViewOptions(plugin.options()->flyViewOptions());
    QQmlEngine engine;
    engine.globalObject().setProperty(QStringLiteral("plugin"),
                                      engine.newQObject(&plugin));
    engine.globalObject().setProperty(QStringLiteral("options"),
                                      engine.newQObject(plugin.options()));
    QVERIFY(!engine.evaluate(QStringLiteral("plugin.customMapItems")).isError());
    // Qt 6.8.3 aborts in qv4qobjectwrapper.cpp:1511 if another engine
    // creates a const wrapper for the same C++ object before this engine's GC.
    QQmlEngine otherEngine;
    otherEngine.globalObject().setProperty(QStringLiteral("plugin"), otherEngine.newQObject(&plugin));
    for (int cycle = 0; cycle < 100; ++cycle) {
        const QJSValue result = otherEngine.evaluate(QStringLiteral(
            "plugin.options.flyView.showMapScale"));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        QVERIFY(result.isBool());
        engine.collectGarbage();
        otherEngine.collectGarbage();
    }
}
