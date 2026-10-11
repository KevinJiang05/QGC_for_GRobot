#include "DeepSharkConnectionMonitorTest.h"

#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtTest/QSignalSpy>
#include <memory>

#include "ColoredSvgImageProvider.h"
#include "DeepSharkConnectionMonitor.h"
#include "DeepSharkVideoController.h"
#include "QGCCorePlugin.h"
#include "Vehicle.h"

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
    m._flightArmed = true;
    m._channels[0].armed = true;
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
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    DeepSharkConnectionMonitor m(nullptr, false);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    const QVariantMap properties{{QStringLiteral("monitor"), QVariant::fromValue(&m)},
                                 {QStringLiteral("width"), 700}};
    for (const QString &file : {QStringLiteral("ConnectionAlertSettings.qml"), QStringLiteral("ConnectionAlertBanner.qml")}) {
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/") + file));
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
    m._flightArmed = true;
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
    m._faultMask = 1; // An active alarm always names the sources it was raised for.
    m._acknowledged = false;
    m._resetReadiness();
    QSignalSpy sounds(&m, &DeepSharkConnectionMonitor::soundRequested);
    m._evaluate(10000);
    QVERIFY(m.alarmActive());
    QCOMPARE(sounds.count(), 1); // Rearming must never silence an existing incident.
}

void DeepSharkConnectionMonitorTest::_steadyStateDoesNotRenotify()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    _fresh(m, 0);
    _fresh(m, 10000);
    QVERIFY(m.monitoring());
    QSignalSpy state(&m, &DeepSharkConnectionMonitor::stateChanged);
    // Nothing a binding can read changes between these ticks.
    _fresh(m, 10500);
    _fresh(m, 11000);
    QCOMPARE(state.count(), 0);
    m._evaluate(14000);
    QVERIFY(m.alarmActive());
    QCOMPARE(state.count(), 1);
    m._evaluate(14500);
    QCOMPARE(state.count(), 1);

    // The confirmation countdown is shown to the user and must keep updating.
    DeepSharkConnectionMonitor starting(nullptr, false);
    _prepare(starting);
    _fresh(starting, 0);
    QSignalSpy startingState(&starting, &DeepSharkConnectionMonitor::stateChanged);
    _fresh(starting, 1000);
    _fresh(starting, 2000);
    QCOMPARE(startingState.count(), 2);
}

void DeepSharkConnectionMonitorTest::_deadCameraDoesNotBlockFlightMonitoring()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m); // The enabled channel never delivers a frame.
    for (qint64 now = 0; now <= 10000; now += 1000) {
        m._lastHeartbeat = now;
        m._evaluate(now);
    }
    QVERIFY(m.monitoring());
    // A camera that never worked is reported as waiting, not as a lost link.
    QVERIFY(!m.alarmActive());
    QVERIFY(m.statusText().contains(QStringLiteral("Left")));

    m._evaluate(14000);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(1));
    for (qint64 now = 15000; now <= 20000; now += 1000) {
        m._lastHeartbeat = now;
        m._evaluate(now);
    }
    QVERIFY(!m.alarmActive());

    // Once the camera has delivered frames for the confirmation period it is monitored as well.
    for (qint64 now = 21000; now <= 31000; now += 1000) {
        _fresh(m, now);
    }
    m._lastHeartbeat = 34000;
    m._evaluate(34000);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(2));
}

void DeepSharkConnectionMonitorTest::_flightIsMonitoredWithoutVideo()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    m._channels[0].expected = false;
    for (qint64 now = 0; now <= 10000; now += 1000) {
        m._lastHeartbeat = now;
        m._evaluate(now);
    }
    QVERIFY(m.monitoring());
    m._evaluate(14000);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(1));
}

void DeepSharkConnectionMonitorTest::_alarmKeepsSourcesThatNeverRecovered()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    DeepSharkConnectionMonitor::Channel right;
    right.name = QStringLiteral("Right");
    right.expected = true;
    m._channels.append(right);
    const auto step = [&m](qint64 now, bool left, bool rightFresh) {
        m._lastHeartbeat = now;
        if (left) { m._channels[0].lastFrame = now; }
        if (rightFresh) { m._channels[1].lastFrame = now; }
        m._evaluate(now);
    };
    for (qint64 now = 0; now <= 10000; now += 1000) { step(now, true, true); }
    step(14000, false, false);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(6));

    // The application was suspended: every source has to prove itself again, the incident stays.
    m._resetReadiness();
    m._lastHeartbeat = -1;
    for (auto &channel : m._channels) { channel.lastFrame = -1; }

    // Only the right camera returns, drops again and returns once more. The left one never does.
    for (qint64 now = 15000; now <= 25000; now += 1000) { step(now, false, true); }
    QVERIFY(m.alarmActive());
    step(29000, false, false);
    for (qint64 now = 30000; now <= 36000; now += 1000) { step(now, false, true); }
    QVERIFY(m.alarmActive());
    QVERIFY(m._faultMask & quint64(2));
    QVERIFY(!m.alarmText().contains(QStringLiteral("已恢复")));
}

void DeepSharkConnectionMonitorTest::_disablingAFaultedCameraEndsItsAlarm_data()
{
    QTest::addColumn<bool>("flightMonitored");
    QTest::newRow("camera-only") << false;
    QTest::newRow("flight-and-camera") << true;
}

void DeepSharkConnectionMonitorTest::_disablingAFaultedCameraEndsItsAlarm()
{
    QFETCH(bool, flightMonitored);
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    const auto step = [&m, flightMonitored](qint64 now, bool cameraFresh) {
        if (flightMonitored) { m._lastHeartbeat = now; }
        if (cameraFresh) { m._channels[0].lastFrame = now; }
        m._evaluate(now);
    };
    for (qint64 now = 0; now <= 10000; now += 1000) { step(now, true); }
    QVERIFY(m.monitoring());
    step(14000, false);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(2));

    // Switching the failed camera off is not a recovery, and it must not leave an alarm that can never end.
    m._channels[0].expected = false;
    step(15000, false);
    QVERIFY(!m.alarmActive());
    QVERIFY(m.attentionRequired());
    QVERIFY(m.alarmText().contains(QStringLiteral("停用")));
    for (qint64 now = 16000; now <= 22000; now += 1000) { step(now, false); }
    QVERIFY(!m.alarmActive());
    QVERIFY(!m.alarmText().contains(QStringLiteral("已恢复")));
}

void DeepSharkConnectionMonitorTest::_switchedOffCameraIsNotReportedAsRecovered()
{
    DeepSharkConnectionMonitor m(nullptr, false);
    _prepare(m);
    DeepSharkConnectionMonitor::Channel right;
    right.name = QStringLiteral("Right");
    right.expected = true;
    m._channels.append(right);
    const auto step = [&m](qint64 now, bool left, bool rightFresh) {
        m._lastHeartbeat = now;
        if (left) { m._channels[0].lastFrame = now; }
        if (rightFresh) { m._channels[1].lastFrame = now; }
        m._evaluate(now);
    };
    for (qint64 now = 0; now <= 10000; now += 1000) { step(now, true, true); }
    step(14000, false, false);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(6));

    // The left camera is switched off while the right one is already delivering again.
    QSignalSpy events(&m, &DeepSharkConnectionMonitor::eventOccurred);
    m._channels[0].expected = false;
    step(15000, false, true);
    QVERIFY(m.alarmActive());
    QCOMPARE(m._faultMask, quint64(4));
    QVERIFY(!m.alarmText().contains(QStringLiteral("Left")));
    QCOMPARE(events.count(), 1);
    QVERIFY(events.constFirst().constFirst().toString().contains(QStringLiteral("Left")));
    QVERIFY(events.constFirst().constFirst().toString().contains(QStringLiteral("停用")));

    // Only the right camera recovers, and only it is named as recovered.
    for (qint64 now = 16000; now <= 21000; now += 1000) { step(now, false, true); }
    QVERIFY(!m.alarmActive());
    QVERIFY(m.alarmText().contains(QStringLiteral("已恢复")));
    QVERIFY(m.alarmText().contains(QStringLiteral("Right")));
    QVERIFY(!m.alarmText().contains(QStringLiteral("Left")));
    QCOMPARE(events.count(), 2);
}

UT_REGISTER_TEST(DeepSharkConnectionMonitorTest, TestLabel::Unit)
