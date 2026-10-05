#include "DeepSharkVideoControllerTest.h"

#include "DeepSharkVideoController.h"
#include "DeepSharkVideoSettings.h"
#include "QGCCorePlugin.h"
#include "QGCOptions.h"
#include "VideoReceiver.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/QJSValue>
#include <memory>
#include <QtCore/qscopeguard.h>

#ifdef QGC_GST_STREAMING
#include "GstVideoReceiver.h"
#include <gst/gst.h>
#endif

void DeepSharkVideoControllerTest::_rtspTransportPreservesCameraUrls()
{
    DeepSharkVideoSettings settings;
    const QString tail = QStringLiteral("://127.0.0.1:554/path%2Fsegment?channel=1&stream=0");
    QCOMPARE(settings.streamUrl(QStringLiteral("rtsp") + tail, DeepSharkVideoSettings::Tcp), QStringLiteral("rtspt") + tail);
    QCOMPARE(settings.streamUrl(QStringLiteral("rtspt") + tail, DeepSharkVideoSettings::Udp), QStringLiteral("rtspu") + tail);
    QCOMPARE(settings.streamUrl(QStringLiteral("rtspu") + tail, DeepSharkVideoSettings::Tcp), QStringLiteral("rtspt") + tail);
    QCOMPARE(settings.streamUrl(QStringLiteral("rtspt") + tail, DeepSharkVideoSettings::Automatic), QStringLiteral("rtspt") + tail);
    QCOMPARE(settings.streamUrl(QStringLiteral("rtsps") + tail, DeepSharkVideoSettings::Tcp), QStringLiteral("rtspst") + tail);
    QCOMPARE(settings.streamUrl(QStringLiteral("rtspst") + tail, DeepSharkVideoSettings::Udp), QStringLiteral("rtspsu") + tail);
    QCOMPARE(settings.streamUrl(QStringLiteral("tcp://127.0.0.1:5600"), DeepSharkVideoSettings::Tcp), QStringLiteral("tcp://127.0.0.1:5600"));
    QVERIFY(settings.streamUrl(QString(), DeepSharkVideoSettings::Tcp).isEmpty());
}

void DeepSharkVideoControllerTest::_videoPanelSavesTransportSelection()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    auto *settings = engine.singletonInstance<DeepSharkVideoSettings *>("DeepShark", "DeepSharkVideoSettings");
    QVERIFY(settings);
    const int previousTransport = settings->rtspTransport();
    QList<QPair<QByteArray, QVariant>> previousProperties;
    for (int channel = 1; channel <= 4; ++channel) {
        for (const QString &suffix : {QStringLiteral("Url"), QStringLiteral("Enabled")}) {
            const QByteArray key = QStringLiteral("camera%1%2").arg(channel).arg(suffix).toUtf8();
            previousProperties.append({key, settings->property(key.constData())});
        }
    }
    const auto restore = qScopeGuard([&]() {
        settings->setRtspTransport(previousTransport);
        for (const auto &entry : previousProperties) {
            settings->setProperty(entry.first.constData(), entry.second);
        }
    });
    settings->setRtspTransport(DeepSharkVideoSettings::Automatic);
    for (int channel = 1; channel <= 4; ++channel) {
        settings->setCameraEnabled(channel, false);
        settings->setProperty(QStringLiteral("camera%1Url").arg(channel).toUtf8().constData(),
                              QStringLiteral("rtsp://127.0.0.1:554/camera%1").arg(channel));
    }
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/FourVideoPanel.qml")),
                            QQmlComponent::PreferSynchronous);
    std::unique_ptr<QObject> panel(component.create());
    QVERIFY2(panel, qPrintable(component.errorString()));
    QObject *combo = panel->findChild<QObject *>(QStringLiteral("rtspTransportCombo"));
    QVERIFY(combo);
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "openSettings"));
    QVERIFY(combo->setProperty("currentIndex", DeepSharkVideoSettings::Tcp));
    QCOMPARE(settings->rtspTransport(), int(DeepSharkVideoSettings::Automatic));
    // Reopening discards an unsaved selection, as cancelling the dialog should.
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "openSettings"));
    QCOMPARE(combo->property("currentIndex").toInt(), int(DeepSharkVideoSettings::Automatic));
    for (int mode : {int(DeepSharkVideoSettings::Tcp), int(DeepSharkVideoSettings::Udp), int(DeepSharkVideoSettings::Automatic)}) {
        QVERIFY(combo->setProperty("currentIndex", mode));
        QVERIFY(QMetaObject::invokeMethod(panel.get(), "saveSettings"));
        QCOMPARE(settings->rtspTransport(), mode);
        DeepSharkVideoSettings reloaded;
        QCOMPARE(reloaded.rtspTransport(), mode);
        const QJSValue rows = panel->property("videoRows").value<QJSValue>();
        for (int index = 0; index < 4; ++index) {
            QObject *tile = rows.property(index).toQObject()->property("tile").value<QObject *>();
            QVERIFY(tile);
            auto *controller = tile->findChild<DeepSharkVideoController *>();
            QVERIFY(controller);
            const QString original = QStringLiteral("rtsp://127.0.0.1:554/camera%1").arg(index + 1);
            QCOMPARE(tile->property("videoSource").toString(), original);
            QCOMPARE(controller->uri(), settings->streamUrl(original, mode));
            QVERIFY(!controller->autoStart());
            QCOMPARE(controller->startAttempts(), 0);
        }
    }
}

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

void DeepSharkVideoControllerTest::_clearingActiveReceiverUriStopsPipeline()
{
#ifndef QGC_GST_STREAMING
    QSKIP("GStreamer video sinks are not enabled");
#else
    GstVideoReceiver receiver;
    QSignalSpy started(&receiver, &VideoReceiver::onStartComplete);
    QSignalSpy stopped(&receiver, &VideoReceiver::onStopComplete);
    receiver.setUri(QStringLiteral("udp://127.0.0.1:0"));
    receiver.start(8);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 5000);
    QCOMPARE(started.constFirst().constFirst().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QVERIFY(receiver._pipeline);

    receiver.setUri(QString());
    receiver.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
    QCOMPARE(stopped.constFirst().constFirst().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    QVERIFY(!receiver._pipeline);

    // The same receiver must be usable after clearing an active URI.
    receiver.setUri(QStringLiteral("udp://127.0.0.1:0"));
    receiver.start(8);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 5000);
    QCOMPARE(started.constLast().constFirst().value<VideoReceiver::STATUS>(), VideoReceiver::STATUS_OK);
    receiver.stop();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 2, 5000);
    QVERIFY(!receiver._pipeline);
#endif
}

void DeepSharkVideoControllerTest::_duplicateDecoderPadsReleaseParentReference()
{
#ifndef QGC_GST_STREAMING
    QSKIP("GStreamer video sinks are not enabled");
#else
    GstVideoReceiver receiver;
    GstElement *pipeline = gst_pipeline_new("duplicate-pad-test");
    GstElement *sink = gst_element_factory_make("fakesink", nullptr);
    QVERIFY(pipeline && sink);
    QVERIFY(gst_bin_add(GST_BIN(pipeline), sink));
    receiver._pipeline = pipeline;
    receiver._videoSink = sink;

    GstCaps *caps = gst_caps_from_string("video/x-raw");
    GstPadTemplate *padTemplate = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    GstPad *pad = gst_pad_new_from_template(padTemplate, "src");
    const int before = GST_OBJECT_REFCOUNT_VALUE(pipeline);
    for (int attempt = 0; attempt < 100; ++attempt) {
        receiver._onNewDecoderPad(pad);
    }
    const int after = GST_OBJECT_REFCOUNT_VALUE(pipeline);

    receiver._videoSink = nullptr;
    receiver._pipeline = nullptr;
    gst_bin_remove(GST_BIN(pipeline), sink);
    gst_clear_object(&pad);
    gst_clear_object(&padTemplate);
    gst_clear_caps(&caps);
    // Keep a failing regression test from retaining the leaked references.
    for (int ref = before; ref < after; ++ref) {
        gst_object_unref(pipeline);
    }
    gst_clear_object(&pipeline);
    QCOMPARE(after, before);
#endif
}

void DeepSharkVideoControllerTest::_videoSinkSizeUsesDecodedCaps()
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
    std::unique_ptr<QObject> tile(component.createWithInitialProperties(
        {{QStringLiteral("videoEnabled"), true}, {QStringLiteral("videoSource"), QString()}}));
    QVERIFY2(tile, qPrintable(component.errorString()));
    auto *controller = tile->findChild<DeepSharkVideoController *>();
    QVERIFY(controller && controller->_sink);
    auto *receiver = qobject_cast<GstVideoReceiver *>(controller->_receiver);
    QVERIFY(receiver);

    const QList<QByteArray> formats{
        "video/x-raw,format=RGBA,width=320,height=240",
        "video/x-raw,format=RGBA"
    };
    for (const QByteArray &format : formats) {
        receiver->_pipeline = gst_pipeline_new("decoded-size-test");
        receiver->_decoderValve = gst_element_factory_make("valve", nullptr);
        QVERIFY(gst_bin_add(GST_BIN(receiver->_pipeline), receiver->_decoderValve));
        receiver->_videoSink = GST_ELEMENT(gst_object_ref(controller->_sink));
        GstPad *valvePad = gst_element_get_static_pad(receiver->_decoderValve, "src");
        const int before = GST_OBJECT_REFCOUNT_VALUE(valvePad);
        GstCaps *caps = gst_caps_from_string(format.constData());
        GstPadTemplate *padTemplate = gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
        GstPad *pad = gst_pad_new_from_template(padTemplate, "src");
        gst_pad_set_active(pad, TRUE);
        gst_pad_push_event(pad, gst_event_new_stream_start("decoded-size-test"));
        gst_pad_push_event(pad, gst_event_new_caps(caps));
        QSignalSpy sizeChanged(receiver, &VideoReceiver::videoSizeChanged);
        const bool linked = receiver->_addVideoSink(pad);
        const int after = GST_OBJECT_REFCOUNT_VALUE(valvePad);
        const QSize reported = sizeChanged.isEmpty() ? QSize() : sizeChanged.constFirst().constFirst().toSize();

        GstPad *sinkPad = gst_element_get_static_pad(receiver->_videoSink, "sink");
        gst_pad_unlink(pad, sinkPad);
        gst_clear_object(&sinkPad);
        receiver->_shutdownDecodingBranch();
        gst_clear_object(&receiver->_pipeline);
        receiver->_decoderValve = nullptr;
        gst_clear_object(&valvePad);
        gst_pad_set_active(pad, FALSE);
        gst_clear_object(&pad);
        gst_clear_object(&padTemplate);
        gst_clear_caps(&caps);

        QVERIFY(linked);
        QCOMPARE(after, before);
        QCOMPARE(sizeChanged.count(), 1);
        QCOMPARE(reported, format.contains("width=") ? QSize(320, 240) : QSize());
    }
#endif
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
