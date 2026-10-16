#include "AIDetectionReceiverTest.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/qscopeguard.h>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlError>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <memory>

#include "AIDetectionManager.h"
#include "AIDetectionReceiver.h"
#include "ColoredSvgImageProvider.h"
#include "DeepSharkVideoSettings.h"
#include "QGCCorePlugin.h"
#include "QGCPalette.h"

namespace {
quint16 availableLocalPort()
{
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return socket.localPort();
}

QJsonObject validDetection(const QString &label = QStringLiteral("target"))
{
    return {
        {QStringLiteral("x"), 0.2},
        {QStringLiteral("y"), 0.1},
        {QStringLiteral("w"), 0.4},
        {QStringLiteral("h"), 0.3},
        {QStringLiteral("confidence"), 0.9},
        {QStringLiteral("label"), label},
    };
}

QJsonObject packet(const QString &sourceId, const QJsonArray &detections, double timestamp)
{
    return {
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("source_id"), sourceId},
        {QStringLiteral("frame_width"), 1280},
        {QStringLiteral("frame_height"), 720},
        {QStringLiteral("detections"), detections},
    };
}

bool sendPacket(QUdpSocket &sender, quint16 port, const QJsonObject &payload)
{
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return sender.writeDatagram(data, QHostAddress::LocalHost, port) == data.size();
}
}

void AIDetectionReceiverTest::_acceptsBoundedPacketAndExpiresIt()
{
    const quint16 port = availableLocalPort();
    QVERIFY(port != 0);

    AIDetectionReceiver receiver;
    receiver.setPort(port);
    receiver.setEnabled(true);
    QVERIFY(receiver.bound());

    QUdpSocket sender;
    const QString longLabel(100, QLatin1Char('x'));
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    QVERIFY(sendPacket(sender, port, packet(QStringLiteral("deepSharkVideo1"), {validDetection(longLabel)}, now)));

    QTRY_COMPARE_WITH_TIMEOUT(receiver.detectionsForSource(QStringLiteral("deepSharkVideo1")).size(), 1, 1000);
    const QVariantMap detection = receiver.detectionsForSource(QStringLiteral("deepSharkVideo1")).constFirst().toMap();
    QCOMPARE(detection.value(QStringLiteral("label")).toString().size(), 64);
    QCOMPARE(detection.value(QStringLiteral("x")).toDouble(), 0.2);
    QCOMPARE(detection.value(QStringLiteral("w")).toDouble(), 0.4);

    QTRY_VERIFY_WITH_TIMEOUT(receiver.detections().isEmpty(), 2500);
}

void AIDetectionReceiverTest::_rejectsOversizedAndStalePackets()
{
    const quint16 port = availableLocalPort();
    QVERIFY(port != 0);

    AIDetectionReceiver receiver;
    receiver.setPort(port);
    receiver.setEnabled(true);
    QVERIFY(receiver.bound());

    QUdpSocket sender;
    const QByteArray oversizedDatagram(32 * 1024 + 1, ' ');
    QString previousStatus = receiver.statusText();
    QCOMPARE(sender.writeDatagram(oversizedDatagram, QHostAddress::LocalHost, port), oversizedDatagram.size());
    QTRY_VERIFY_WITH_TIMEOUT(receiver.statusText() != previousStatus, 1000);
    QVERIFY(receiver.detections().isEmpty());

    QJsonArray tooManyDetections;
    for (int index = 0; index < 65; ++index) {
        tooManyDetections.append(validDetection());
    }
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    previousStatus = receiver.statusText();
    QVERIFY(sendPacket(sender, port, packet(QStringLiteral("deepSharkVideo1"), tooManyDetections, now)));
    QTRY_VERIFY_WITH_TIMEOUT(receiver.statusText() != previousStatus, 1000);
    QVERIFY(receiver.detections().isEmpty());

    previousStatus = receiver.statusText();
    QVERIFY(sendPacket(sender, port, packet(QStringLiteral("deepSharkVideo1"), {validDetection()}, now - 60.0)));
    QTRY_VERIFY_WITH_TIMEOUT(receiver.statusText() != previousStatus, 1000);
    QVERIFY(receiver.detections().isEmpty());

    previousStatus = receiver.statusText();
    QVERIFY(sendPacket(sender, port, packet(QStringLiteral("bad/source"), {validDetection()}, now)));
    QTRY_VERIFY_WITH_TIMEOUT(receiver.statusText() != previousStatus, 1000);
    QVERIFY(receiver.detections().isEmpty());
}

void AIDetectionReceiverTest::_disableClearsDetections()
{
    const quint16 port = availableLocalPort();
    QVERIFY(port != 0);

    AIDetectionReceiver receiver;
    receiver.setPort(port);
    receiver.setEnabled(true);

    QUdpSocket sender;
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    QVERIFY(sendPacket(sender, port, packet(QStringLiteral("deepSharkVideo1"), {validDetection()}, now)));
    QTRY_COMPARE_WITH_TIMEOUT(receiver.detections().size(), 1, 1000);

    receiver.setEnabled(false);
    QVERIFY(receiver.detections().isEmpty());
    QVERIFY(!receiver.bound());
}

void AIDetectionReceiverTest::_overlaySwitchMigratesLegacyVideoSetting()
{
    const QString overlayKey = QStringLiteral("AIDetection/OverlayEnabled");
    const QString legacyKey = QStringLiteral("Video/yoloOverlay");
    QSettings settings;
    settings.remove(overlayKey);
    settings.setValue(legacyKey, false);

    {
        AIDetectionManager manager;
        QVERIFY(!manager.overlayEnabled());
        manager.setOverlayEnabled(true);
    }

    // Once the new key exists, the legacy value no longer applies.
    AIDetectionManager reloaded;
    QVERIFY(reloaded.overlayEnabled());

    settings.remove(overlayKey);
    settings.remove(legacyKey);
}

void AIDetectionReceiverTest::_startManagedStub(AIDetectionManager* manager)
{
    manager->_deleteProcess(manager->_detectProcess);
    manager->_inputRestartRequired = false;
    manager->_restartPending = false;
    manager->_stopRequested = false;
    emit manager->acceptingDetectionsChanged();
    auto* process = new QProcess(manager);
    manager->_detectProcess = process;
    connect(process, &QProcess::started, manager, &AIDetectionManager::_detectStarted);
    connect(process, &QProcess::finished, manager,
            [manager](int exitCode, QProcess::ExitStatus) { manager->_detectFinished(exitCode); });
    connect(process, &QProcess::errorOccurred, manager, &AIDetectionManager::_detectError);
    // A harmless process waits for stdin. It exercises the real asynchronous stop/finished
    // lifecycle without importing Python libraries, opening a camera, or running inference.
#ifdef Q_OS_WIN
    process->start(QStringLiteral("cmd.exe"), {QStringLiteral("/d"), QStringLiteral("/c"), QStringLiteral("more")});
#else
    process->start(QStringLiteral("cat"), {});
#endif
    QTRY_COMPARE_WITH_TIMEOUT(process->state(), QProcess::Running, 2000);
}

void AIDetectionReceiverTest::_runningInputChangeStopsSession_data()
{
    QTest::addColumn<QByteArray>("inputProperty");
    for (const char* name : {"udpPort", "camera1Url", "camera2Url", "camera3Url", "camera4Url", "camera1Enabled",
                             "camera2Enabled", "camera3Enabled", "camera4Enabled"}) {
        QTest::newRow(name) << QByteArray(name);
    }
}

void AIDetectionReceiverTest::_runningInputChangeStopsSession()
{
    QFETCH(QByteArray, inputProperty);
    QGCCorePlugin::instance()->init();
    auto* manager = QCoreApplication::instance()->findChild<AIDetectionManager*>();
    auto* receiver = QCoreApplication::instance()->findChild<AIDetectionReceiver*>();
    auto* videoSettings = QGCCorePlugin::instance()->findChild<DeepSharkVideoSettings*>();
    QVERIFY(manager);
    QVERIFY(receiver);
    QVERIFY(videoSettings);
    const int savedPort = manager->udpPort();
    const QVariant savedInput = videoSettings->property(inputProperty.constData());
    QMetaObject::Connection portConnection;
    const auto cleanup = qScopeGuard([&]() {
        disconnect(portConnection);
        manager->_deleteProcess(manager->_detectProcess);
        manager->_restartPending = false;
        manager->_stopRequested = false;
        manager->_inputRestartRequired = false;
        if (inputProperty != "udpPort") {
            videoSettings->setProperty(inputProperty.constData(), savedInput);
        }
        manager->setUdpPort(savedPort);
        emit manager->acceptingDetectionsChanged();
    });
    const quint16 oldPort = availableLocalPort();
    QVERIFY(oldPort != 0);
    manager->setUdpPort(oldPort);
    _startManagedStub(manager);
    QVERIFY(manager->running());
    QVERIFY(receiver->enabled());
    QVERIFY(receiver->bound());

    QUdpSocket sender;
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    QVERIFY(sendPacket(sender, oldPort, packet(QStringLiteral("deepSharkVideo1"), {validDetection()}, now)));
    QTRY_COMPARE_WITH_TIMEOUT(receiver->detections().size(), 1, 1000);
    bool reboundWhileRunning = false;
    portConnection = connect(receiver, &AIDetectionReceiver::portChanged, this,
                             [&]() { reboundWhileRunning |= manager->running(); });

    int expectedPort = oldPort;
    if (inputProperty == "udpPort") {
        expectedPort = availableLocalPort();
        QVERIFY(expectedPort != 0);
        manager->setUdpPort(expectedPort);
        QCOMPARE(receiver->port(), oldPort);
    } else if (inputProperty.endsWith("Enabled")) {
        QVERIFY(videoSettings->setProperty(inputProperty.constData(), !savedInput.toBool()));
    } else {
        QVERIFY(videoSettings->setProperty(inputProperty.constData(),
                                           savedInput.toString() + QStringLiteral("/different-test-source")));
    }
    QVERIFY(!receiver->enabled());
    QVERIFY(!receiver->bound());
    QVERIFY(receiver->detections().isEmpty());
    manager->saveSettings();
    QVERIFY(manager->statusText().contains(QStringLiteral("Restart AI")));
    QTRY_VERIFY_WITH_TIMEOUT(!manager->running(), 3000);
    QVERIFY(!reboundWhileRunning);
    QCOMPARE(receiver->port(), static_cast<quint16>(expectedPort));
    QVERIFY(!receiver->enabled());
    QVERIFY(receiver->detections().isEmpty());
    QVERIFY(!manager->_restartPending);
    QVERIFY(manager->statusText().contains(QStringLiteral("Restart AI")));
    QVERIFY(sendPacket(sender, static_cast<quint16>(expectedPort),
                       packet(QStringLiteral("deepSharkVideo1"), {validDetection(QStringLiteral("old frame"))}, now)));
    QVERIFY(!receiver->bound());
    QVERIFY(receiver->detections().isEmpty());
}

void AIDetectionReceiverTest::_unchangedInputSettingsKeepSession()
{
    QGCCorePlugin::instance()->init();
    auto* manager = QCoreApplication::instance()->findChild<AIDetectionManager*>();
    auto* videoSettings = QGCCorePlugin::instance()->findChild<DeepSharkVideoSettings*>();
    QVERIFY(manager);
    QVERIFY(videoSettings);
    const auto cleanup = qScopeGuard([&]() {
        manager->_deleteProcess(manager->_detectProcess);
        emit manager->runningChanged();
    });
    _startManagedStub(manager);
    QVERIFY(manager->running());
    QProcess* process = manager->_detectProcess;
    for (const char* name : {"camera1Url", "camera2Url", "camera3Url", "camera4Url", "camera1Enabled", "camera2Enabled",
                             "camera3Enabled", "camera4Enabled"}) {
        QVERIFY(videoSettings->setProperty(name, videoSettings->property(name)));
    }
    manager->setUdpPort(manager->udpPort());
    manager->saveSettings();
    QCOMPARE(manager->_detectProcess, process);
    QVERIFY(manager->running());
    QVERIFY(!manager->_stopRequested);
    QVERIFY(manager->acceptingDetections());
}

void AIDetectionReceiverTest::_stoppedInputChangesDoNotStart()
{
    AIDetectionManager manager;
    const int oldPort = manager.udpPort();
    QSignalSpy running(&manager, &AIDetectionManager::runningChanged);
    manager.setUdpPort(manager.udpPort() == 57610 ? 57611 : 57610);
    manager.inputConfigurationChanged();
    manager.saveSettings();
    QVERIFY(!manager.running());
    QCOMPARE(running.count(), 0);
    QVERIFY(!manager._restartPending);
    manager.setUdpPort(oldPort);
}

void AIDetectionReceiverTest::_qmlComponentsLoad()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    engine.rootContext()->setContextProperty(QStringLiteral("qgcPal"), new QGCPalette(&engine));
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);

    const auto createComponent = [&engine](const QString &url) {
        QQmlComponent component(&engine, QUrl(url), QQmlComponent::PreferSynchronous);
        QVERIFY2(component.isReady(), qPrintable(url + QStringLiteral(": ") + component.errorString()));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(url + QStringLiteral(": ") + component.errorString()));
    };

    createComponent(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/AIDetectionSettings.qml"));
    createComponent(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/AIDetectionVideoOverlay.qml"));
    QCOMPARE(warnings.count(), 0);

    // Provide the main-window palette so the standalone upstream settings page has its normal context.
    createComponent(QStringLiteral("qrc:/qml/QGroundControl/AppSettings/VideoSettings.qml"));
    for (const QList<QVariant> &arguments : std::as_const(warnings)) {
        for (const QQmlError &error : arguments.constFirst().value<QList<QQmlError>>()) {
            QVERIFY2(!error.toString().contains(QStringLiteral("AIDetection")), qPrintable(error.toString()));
        }
    }
}

UT_REGISTER_TEST(AIDetectionReceiverTest, TestLabel::Unit)
