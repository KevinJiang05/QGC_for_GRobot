#include "DeepSharkConnectionMonitorTest.h"
#include "DeepSharkConnectionMonitor.h"
#include "DeepSharkVideoController.h"
#include "QGCCorePlugin.h"
#include "Vehicle.h"
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QSignalSpy>
#include <memory>

void DeepSharkConnectionMonitorTest::_prepare(DeepSharkConnectionMonitor &m)
{
    DeepSharkConnectionMonitor::Channel channel;
    channel.name = QStringLiteral("Left");
    channel.expected = true;
    m._channels.append(channel);
    m._flightPresent = true;
}

void DeepSharkConnectionMonitorTest::_fresh(DeepSharkConnectionMonitor &m, qint64 now)
{
    m._lastHeartbeat = now;
    m._channels[0].lastFrame = now;
    m._evaluate(now);
}

void DeepSharkConnectionMonitorTest::_startupRequiresStableLinks()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    QSignalSpy sounds(&m, &DeepSharkConnectionMonitor::soundRequested);
    m._evaluate(60000);
    QVERIFY(!m.monitoring());
    QVERIFY(!m.alarmActive());
    _fresh(m, 61000);
    _fresh(m, 70000);
    QVERIFY(!m.monitoring());
    m._evaluate(74000); // A break restarts the initial confirmation period.
    _fresh(m, 75000);
    _fresh(m, 84999);
    QVERIFY(!m.monitoring());
    _fresh(m, 85000);
    QVERIFY(m.monitoring());
    QCOMPARE(sounds.count(), 0);
}

void DeepSharkConnectionMonitorTest::_videoAndHeartbeatFaults()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    _fresh(m, 0);
    _fresh(m, 10000);
    QSignalSpy sounds(&m, &DeepSharkConnectionMonitor::soundRequested);
    m._lastHeartbeat = 12999;
    m._evaluate(12999);
    QVERIFY(!m.alarmActive());
    m._evaluate(13000);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(2));
    QCOMPARE(sounds.count(), 1);
    m.acknowledge();
    m._evaluate(16000);
    QCOMPARE(sounds.count(), 1);
    m._evaluate(17000); // A new flight fault must sound even after video acknowledgement.
    QCOMPARE(m._faultMask, quint64(3));
    QCOMPARE(sounds.count(), 2);
    QVERIFY(!m.acknowledged());
    m.setSoundEnabled(false);
    m._evaluate(22000);
    QCOMPARE(sounds.count(), 2);
    m.setSoundEnabled(true);
    m._evaluate(22001);
    QCOMPARE(sounds.count(), 3);

    DeepSharkConnectionMonitor flight(nullptr, false);
    _prepare(flight);
    _fresh(flight, 0);
    _fresh(flight, 10000);
    flight._channels[0].lastFrame = 14000;
    flight._evaluate(14000);
    QCOMPARE(flight._faultMask, quint64(1));
}

void DeepSharkConnectionMonitorTest::_recoveryAndAcknowledgement()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    _fresh(m, 0);
    _fresh(m, 10000);
    m._evaluate(14000);
    QVERIFY(m.alarmActive());
    _fresh(m, 15000);
    _fresh(m, 19999);
    QVERIFY(m.alarmActive());
    _fresh(m, 20000);
    QVERIFY(!m.alarmActive());
    QVERIFY(m.attentionRequired());
    m.acknowledge();
    QVERIFY(!m.attentionRequired());
    m._evaluate(24000);
    QVERIFY(m.alarmActive());
    QVERIFY(!m.acknowledged());
    m.acknowledge();
    _fresh(m, 25000);
    _fresh(m, 30000);
    QVERIFY(!m.attentionRequired());
}

void DeepSharkConnectionMonitorTest::_manualActionsAndMasterSwitch()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    DeepSharkVideoController controller;
    _prepare(m);
    m._channels[0].controller = &controller;
    _fresh(m, 0);
    _fresh(m, 10000);
    m.manualReconnect(&controller);
    QVERIFY(m._channels[0].graceUntil >= 5000);
    m._channels[0].graceUntil = 15000;
    m._lastHeartbeat = 14000;
    m._evaluate(14000);
    QVERIFY(!m.alarmActive());
    m._evaluate(15000);
    QVERIFY(m.alarmActive());
    m.manualReconnect(&controller);
    QCOMPARE(m._channels[0].graceUntil, qint64(15000));
    m._channels[0].expected = false; // Stopping one video does not disable flight monitoring.
    m._evaluate(18000);
    QCOMPARE(m._faultMask, quint64(1));
    m._plannedDisconnect();
    QVERIFY(!m.monitoring());
    QVERIFY(!m.alarmActive());
    QVERIFY(m.attentionRequired());
    m.setEnabled(false);
    QVERIFY(!m.attentionRequired());
    m.setEnabled(true);
    m._evaluate(60000);
    QVERIFY(!m.alarmActive());
}

void DeepSharkConnectionMonitorTest::_frameProgressSurvivesReconnect()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    auto controller = std::make_unique<DeepSharkVideoController>();
    controller->setReceiverName(QStringLiteral("left"));
    controller->_alertExpected = true;
    m.track(controller.get());
    m._tick();
    QCOMPARE(m._channels[0].lastFrame, qint64(-1));
    controller->_totalFrameCount = 1;
    m._tick();
    QVERIFY(m._channels[0].lastFrame >= 0);
    m._channels[0].lastFrame = -100;
    controller->_sinkFrameCount = 0; // Receiver statistics reset is not a new frame.
    m._tick();
    QCOMPARE(m._channels[0].lastFrame, qint64(-100));
    controller.reset();
    m._armed = true;
    m._lastHeartbeat = m._clock.elapsed();
    m._flightPresent = true;
    m._channels[0].lastFrame = -1;
    m._channels[0].graceUntil = 0;
    m._tick();
    QVERIFY(m.alarmActive()); // Destroying a receiver cannot silently exempt a failed channel.
    controller = std::make_unique<DeepSharkVideoController>();
    controller->setReceiverName(QStringLiteral("left"));
    controller->_alertTitle = QStringLiteral("Renamed");
    controller->_alertExpected = true;
    m.track(controller.get());
    QCOMPARE(m._channels.size(), 1);
    m._tick();
    QVERIFY(m.alarmActive());
    controller->_totalFrameCount = 1;
    m._tick();
    QVERIFY(m._channels[0].lastFrame >= 0);
    QVERIFY(m.alarmActive()); // Recovery needs its own stable confirmation window.
}

void DeepSharkConnectionMonitorTest::_settingsRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("alert.ini")), QSettings::IniFormat);
    DeepSharkConnectionMonitor m(nullptr, false);
    m.setEnabled(false);
    m.setSoundEnabled(false);
    m.setVideoTimeout(7);
    m.setSoundType(1);
    m.setHeartbeatTimeout(8);
    m.setReadySeconds(12);
    m.setRecoverySeconds(6);
    m.setRepeatSeconds(9);
    m._saveSettings(settings);
    DeepSharkConnectionMonitor loaded(nullptr, false);
    loaded._loadSettings(settings);
    QVERIFY(!loaded.enabled());
    QVERIFY(!loaded.soundEnabled());
    QCOMPARE(loaded.soundType(), 1);
    QCOMPARE(loaded.videoTimeout(), 7);
    QCOMPARE(loaded.heartbeatTimeout(), 8);
    QCOMPARE(loaded.readySeconds(), 12);
    QCOMPARE(loaded.recoverySeconds(), 6);
    QCOMPARE(loaded.repeatSeconds(), 9);
    settings.setValue(QStringLiteral("DeepShark/ConnectionAlert/VideoTimeout"), -5);
    settings.setValue(QStringLiteral("DeepShark/ConnectionAlert/HeartbeatTimeout"), QStringLiteral("invalid"));
    loaded._loadSettings(settings);
    QCOMPARE(loaded.videoTimeout(), 2);
    QCOMPARE(loaded.heartbeatTimeout(), 4);
    loaded.setReadySeconds(999);
    QCOMPARE(loaded.readySeconds(), 60);
    loaded.setSoundType(99);
    QCOMPARE(loaded.soundType(), 1);
    loaded.setSoundType(-1);
    QCOMPARE(loaded.soundType(), 0);
}

void DeepSharkConnectionMonitorTest::_qmlControlsLoad()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    DeepSharkConnectionMonitor m(nullptr, false);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    const QVariantMap properties{{QStringLiteral("monitor"), QVariant::fromValue(&m)},
                                 {QStringLiteral("width"), 700}};
    for (const QString &file : {QStringLiteral("ConnectionAlertSettings.qml"), QStringLiteral("ConnectionAlertBanner.qml")}) {
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlightDisplay/DeepShark/") + file));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(properties));
        QVERIFY2(object, qPrintable(component.errorString()));
        QVERIFY(object->property("implicitHeight").toDouble() > 0);
        m.setEnabled(false);
        QCoreApplication::processEvents();
        if (file.contains(QStringLiteral("Banner"))) { QVERIFY(!object->property("visible").toBool()); }
        m.setEnabled(true);
    }
    QCOMPARE(warnings.count(), 0);
}

void DeepSharkConnectionMonitorTest::_vehicleSignals()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    Vehicle vehicle(MAV_AUTOPILOT_ARDUPILOTMEGA, MAV_TYPE_SUBMARINE, nullptr);
    m._setVehicle(&vehicle);
    mavlink_message_t heartbeat{};
    heartbeat.msgid = MAVLINK_MSG_ID_HEARTBEAT;
    heartbeat.sysid = vehicle.id() + 1;
    heartbeat.compid = vehicle.defaultComponentId();
    emit vehicle.mavlinkMessageReceived(heartbeat);
    QCOMPARE(m._lastHeartbeat, qint64(-1));
    heartbeat.sysid = vehicle.id();
    heartbeat.compid = vehicle.defaultComponentId() + 1;
    emit vehicle.mavlinkMessageReceived(heartbeat);
    QCOMPARE(m._lastHeartbeat, qint64(-1));
    heartbeat.compid = vehicle.defaultComponentId();
    emit vehicle.mavlinkMessageReceived(heartbeat);
    QVERIFY(m._lastHeartbeat >= 0);
    m._armed = true;
    m._channels[0].lastFrame = 0;
    m._setVehicle(nullptr);
    m._evaluate(0);
    QCOMPARE(m._faultMask, quint64(1));
    m._setVehicle(&vehicle);
    QVERIFY(m.alarmActive());
    emit vehicle.connectionCloseRequested();
    QVERIFY(!m.monitoring());
    QVERIFY(!m.alarmActive());
    QVERIFY(m.attentionRequired());
}

void DeepSharkConnectionMonitorTest::_existingAlarmSurvivesRearm()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    m._armed = true;
    m._alarmActive = true;
    m._acknowledged = false;
    m._resetReadiness();
    QSignalSpy sounds(&m, &DeepSharkConnectionMonitor::soundRequested);
    m._evaluate(10000);
    QVERIFY(m.alarmActive());
    QCOMPARE(sounds.count(), 1); // Rearming must never silence an existing incident.
}
