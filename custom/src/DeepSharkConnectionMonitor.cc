#include "DeepSharkConnectionMonitor.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSettings>
#include <QtCore/qapplicationstatic.h>
#include <QtMultimedia/QSoundEffect>
#include <algorithm>

#include "AppSettings.h"
#include "DeepSharkVideoController.h"
#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

Q_APPLICATION_STATIC(DeepSharkConnectionMonitor, connectionMonitor);
QGC_LOGGING_CATEGORY(DeepSharkConnectionMonitorLog, "qgc.deepshark.connectionalert")

DeepSharkConnectionMonitor *DeepSharkConnectionMonitor::instance()
{
    return connectionMonitor();
}

DeepSharkConnectionMonitor::DeepSharkConnectionMonitor(QObject *parent, bool live)
    : QObject(parent), _live(live)
{
    if (_live) { QSettings settings; _loadSettings(settings); }
    _clock.start();
    _timer.setInterval(500);
    connect(&_timer, &QTimer::timeout, this, &DeepSharkConnectionMonitor::_tick);
    connect(this, &DeepSharkConnectionMonitor::eventOccurred, this, [](const QString &message) {
        qCInfo(DeepSharkConnectionMonitorLog).noquote() << message;
    });
    _statusText = _enabled ? tr("等待飞控和视频就绪") : tr("断联预警已关闭");
    connect(this, &DeepSharkConnectionMonitor::stateChanged, this, [this] { _notifiedState = _currentState(); });
}

DeepSharkConnectionMonitor::NotifiedState DeepSharkConnectionMonitor::_currentState() const
{
    // audioStatus follows the application mute setting, which has no signal wired to this object.
    return {_armed, _alarmActive, _pendingAcknowledgement, _acknowledged, _statusText, _alarmText, audioStatus()};
}

void DeepSharkConnectionMonitor::_notifyStateIfChanged()
{
    // The state properties share one signal; a tick that changed none of them stays silent.
    if (!(_notifiedState == _currentState())) {
        emit stateChanged();
    }
}

void DeepSharkConnectionMonitor::start()
{
    if (_started || !_live || qgcApp()->runningUnitTests()) { return; }
    _started = true;
    auto *vehicles = MultiVehicleManager::instance();
    connect(vehicles, &MultiVehicleManager::activeVehicleChanged, this, &DeepSharkConnectionMonitor::_setVehicle);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        _timer.stop();
        _soundPending = false;
        if (_sound) { _sound->stop(); }
    });
    _setVehicle(vehicles->activeVehicle());
    _timer.start();
}

void DeepSharkConnectionMonitor::_loadSettings(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("DeepShark/ConnectionAlert"));
    _enabled = settings.value("Enabled", true).toBool();
    _soundEnabled = settings.value("SoundEnabled", true).toBool();
    const auto bounded = [&settings](const char *key, int fallback, int low, int high) {
        bool valid = false;
        const int value = settings.value(QLatin1String(key), fallback).toInt(&valid);
        return valid ? std::clamp(value, low, high) : fallback;
    };
    _videoTimeout = bounded("VideoTimeout", 3, 2, 30);
    _soundType = bounded("SoundType", 0, 0, 1);
    _heartbeatTimeout = bounded("HeartbeatTimeout", 4, 2, 30);
    _readySeconds = bounded("ReadySeconds", 10, 3, 60);
    _recoverySeconds = bounded("RecoverySeconds", 5, 2, 30);
    _repeatSeconds = bounded("RepeatSeconds", 3, 2, 30);
    settings.endGroup();
}

void DeepSharkConnectionMonitor::_saveSettings(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("DeepShark/ConnectionAlert"));
    settings.setValue("Enabled", _enabled);
    settings.setValue("SoundEnabled", _soundEnabled);
    settings.setValue("SoundType", _soundType);
    settings.setValue("VideoTimeout", _videoTimeout);
    settings.setValue("HeartbeatTimeout", _heartbeatTimeout);
    settings.setValue("ReadySeconds", _readySeconds);
    settings.setValue("RecoverySeconds", _recoverySeconds);
    settings.setValue("RepeatSeconds", _repeatSeconds);
    settings.endGroup();
}

void DeepSharkConnectionMonitor::_persist()
{
    if (_live) { QSettings settings; _saveSettings(settings); }
    emit settingsChanged();
}

void DeepSharkConnectionMonitor::_resetReadiness()
{
    _armed = false;
    _flightArmed = false;
    _flightReadySince = -1;
    for (auto &channel : _channels) {
        channel.armed = false;
        channel.readySince = -1;
    }
    _recoverySince = -1;
}

void DeepSharkConnectionMonitor::setEnabled(bool value)
{
    if (_enabled == value) { return; }
    _enabled = value;
    _resetReadiness();
    if (!value) {
        _alarmActive = false;
        _pendingAcknowledgement = false;
        _acknowledged = false;
        _faultMask = 0;
        _alarmText.clear();
        _soundPending = false;
        if (_sound) { _sound->stop(); }
        emit eventOccurred(tr("用户关闭断联预警"));
    }
    _statusText = value ? tr("等待飞控和视频就绪") : tr("断联预警已关闭");
    _persist();
    emit stateChanged();
}

void DeepSharkConnectionMonitor::setSoundEnabled(bool value)
{
    if (_soundEnabled == value) { return; }
    _soundEnabled = value;
    if (!value) {
        _soundPending = false;
        if (_sound) { _sound->stop(); }
    }
    _lastSound = -1;
    _persist();
    emit stateChanged();
}

void DeepSharkConnectionMonitor::_setThreshold(int &field, int value, int low, int high)
{
    value = std::clamp(value, low, high);
    if (field == value) { return; }
    field = value;
    // Do not clear incidents or their acknowledgement when settings change.
    _flightReadySince = -1;
    for (auto &channel : _channels) { channel.readySince = -1; }
    _recoverySince = -1;
    _persist();
}

void DeepSharkConnectionMonitor::setSoundType(int value)
{
    value = std::clamp(value, 0, 1);
    if (_soundType == value) { return; }
    _soundType = value;
    _soundPending = false;
    if (_sound) {
        _sound->stop();
        delete _sound;
        _sound = nullptr;
    }
    _lastSound = -1;
    _persist();
    emit stateChanged();
}
void DeepSharkConnectionMonitor::setVideoTimeout(int v) { _setThreshold(_videoTimeout, v, 2, 30); }
void DeepSharkConnectionMonitor::setHeartbeatTimeout(int v) { _setThreshold(_heartbeatTimeout, v, 2, 30); }
void DeepSharkConnectionMonitor::setReadySeconds(int v) { _setThreshold(_readySeconds, v, 3, 60); }
void DeepSharkConnectionMonitor::setRecoverySeconds(int v) { _setThreshold(_recoverySeconds, v, 2, 30); }
void DeepSharkConnectionMonitor::setRepeatSeconds(int v) { _setThreshold(_repeatSeconds, v, 2, 30); }

void DeepSharkConnectionMonitor::track(DeepSharkVideoController *controller)
{
    if (!controller) { return; }
    for (auto &channel : _channels) {
        if (channel.controller == controller) { return; }
        if (!channel.controller && channel.id == controller->receiverName()) {
            channel.controller = controller;
            channel.count = 0;
            return; // Preserve last progress/incident across object recreation.
        }
    }
    if (_channels.size() < 4) {
        Channel channel;
        channel.controller = controller;
        channel.name = controller->alertTitle();
        channel.id = controller->receiverName();
        _channels.append(channel);
    }
}

void DeepSharkConnectionMonitor::manualReconnect(DeepSharkVideoController *controller)
{
    if (_alarmActive) { return; }
    for (auto &channel : _channels) {
        if (channel.controller == controller) {
            channel.graceUntil = _clock.elapsed() + 5000;
            emit eventOccurred(tr("%1：手动重连，预警宽限 5 秒").arg(channel.name));
        }
    }
}

void DeepSharkConnectionMonitor::_setVehicle(Vehicle *vehicle)
{
    if (_vehicle == vehicle) { return; }
    if (_vehicle) { disconnect(_vehicle, nullptr, this, nullptr); }
    _vehicle = vehicle;
    _flightPresent = vehicle != nullptr;
    if (!vehicle) { return; } // Unexpected removal must remain an alarm source.
    if (_vehicleId != -1 && _vehicleId != vehicle->id()) {
        _resetReadiness();
        emit eventOccurred(tr("飞控监测对象已变更，重新等待链路就绪"));
    }
    _vehicleId = vehicle->id();
    _lastHeartbeat = -1;
    connect(vehicle, &Vehicle::mavlinkMessageReceived, this, [this](const mavlink_message_t &message) {
        if (_vehicle && message.msgid == MAVLINK_MSG_ID_HEARTBEAT
                && message.sysid == _vehicle->id() && message.compid == _vehicle->defaultComponentId()) {
            _lastHeartbeat = _clock.elapsed();
        }
    });
    connect(vehicle, &Vehicle::connectionCloseRequested, this, &DeepSharkConnectionMonitor::_plannedDisconnect);
    connect(vehicle, &QObject::destroyed, this, [this] { _flightPresent = false; });
}

void DeepSharkConnectionMonitor::prepareLinkDisconnect(QObject *link)
{
    auto *linkInterface = qobject_cast<LinkInterface *>(link);
    if (_vehicle && linkInterface && _vehicle->vehicleLinkManager()->containsLink(linkInterface)
            && _vehicle->vehicleLinkManager()->linkNames().size() == 1) {
        _plannedDisconnect();
    }
}

void DeepSharkConnectionMonitor::_plannedDisconnect()
{
    _resetReadiness();
    _lastHeartbeat = -1;
    // A prior incident remains visible until acknowledged even on deliberate disconnect.
    _alarmActive = false;
    _faultMask = 0;
    _pendingAcknowledgement = _pendingAcknowledgement && !_acknowledged;
    _soundPending = false;
    if (_sound) { _sound->stop(); }
    _statusText = tr("飞控已主动断开，等待重新连接");
    emit eventOccurred(_statusText);
    emit stateChanged();
}

void DeepSharkConnectionMonitor::_tick()
{
    const qint64 now = _clock.elapsed();
    // A suspended/stalled application cannot infer a vehicle failure from its own time gap.
    if (_lastTick >= 0 && now - _lastTick > 5000) {
        _resetReadiness();
        _lastHeartbeat = -1;
        for (auto &channel : _channels) { channel.lastFrame = -1; }
        if (_enabled && !_alarmActive) {
            _pendingAcknowledgement = true;
            _acknowledged = false;
            _alarmText = tr("监测曾中断，请检查当前链路状态");
        }
        emit eventOccurred(tr("监测曾中断，等待新的飞控心跳与视频帧"));
    }
    _lastTick = now;
    for (auto &channel : _channels) {
        if (!channel.controller) { continue; }
        const bool expected = channel.controller->alertExpected();
        if (expected && !channel.expected) {
            channel.lastFrame = -1;
            channel.count = channel.controller->totalFrameCount();
            channel.graceUntil = now + 5000;
            channel.armed = false;
            channel.readySince = -1;
        }
        channel.expected = expected;
        channel.name = channel.controller->alertTitle();
        const quint64 count = channel.controller->totalFrameCount();
        if (expected && count > channel.count) { channel.lastFrame = now; }
        channel.count = count;
    }
    _evaluate(now);
}

void DeepSharkConnectionMonitor::_evaluate(qint64 now)
{
    if (!_enabled) { return; }
    const bool flightFresh = _flightPresent && _lastHeartbeat >= 0 && now - _lastHeartbeat < _heartbeatTimeout * 1000;
    // A source joins the monitoring on its own. A camera that never delivered a frame must not keep
    // the flight link, or the other cameras, unmonitored.
    const auto confirm = [this, now](bool fresh, qint64 &readySince, bool &armed) {
        if (armed) { return false; }
        if (!fresh) { readySince = -1; return false; }
        if (readySince < 0) { readySince = now; }
        armed = now - readySince >= _readySeconds * 1000;
        return armed;
    };
    QStringList newlyArmed;
    if (confirm(flightFresh, _flightReadySince, _flightArmed)) { newlyArmed << tr("飞控"); }
    quint64 faults = _flightArmed && !flightFresh ? 1 : 0;
    // _faultMask holds every source the active alarm was raised for. A switched-off source leaves it at
    // once; the others stay until all of them have been healthy for the recovery period, never dropping
    // out because they are momentarily unmonitored.
    quint64 alarmSources = _faultMask;
    bool alarmSourcesFresh = !(alarmSources & 1) || flightFresh;
    QStringList retired;
    QStringList waiting;
    if (!_flightArmed && !flightFresh) { waiting << tr("飞控心跳"); }
    qint64 confirmingSince = _flightArmed ? -1 : _flightReadySince;
    int armedVideos = 0;
    for (int i = 0; i < _channels.size(); ++i) {
        auto &channel = _channels[i];
        const quint64 bit = quint64(1) << (i + 1);
        if (!channel.expected) {
            channel.armed = false;
            channel.readySince = -1;
            if (alarmSources & bit) {
                alarmSources &= ~bit;
                retired << channel.name;
            }
            continue;
        }
        const bool fresh = channel.lastFrame >= 0 && now - channel.lastFrame < _videoTimeout * 1000;
        if (confirm(fresh, channel.readySince, channel.armed)) { newlyArmed << channel.name; }
        if (channel.armed) {
            ++armedVideos;
            if (!fresh && now >= channel.graceUntil) { faults |= bit; }
        } else if (!fresh) {
            waiting << tr("%1视频新帧").arg(channel.name);
        } else if (confirmingSince < 0 || channel.readySince < confirmingSince) {
            confirmingSince = channel.readySince;
        }
        if ((alarmSources & bit) && !fresh) { alarmSourcesFresh = false; }
    }
    if (!newlyArmed.isEmpty()) {
        emit eventOccurred(tr("链路监测已就绪：%1").arg(newlyArmed.join(tr("、"))));
    }
    _armed = _flightArmed || armedVideos > 0;
    if (!_armed) {
        _statusText = confirmingSince >= 0
                ? tr("链路就绪确认：%1 / %2 秒").arg((now - confirmingSince) / 1000).arg(_readySeconds)
                : tr("等待链路就绪：%1").arg(waiting.join(tr("、")));
    } else {
        _statusText = !_flightArmed ? tr("链路监测中：%1 路视频").arg(armedVideos)
                    : armedVideos > 0 ? tr("链路监测中：飞控 + %1 路视频").arg(armedVideos) : tr("链路监测中：飞控");
        if (!waiting.isEmpty()) {
            _statusText += tr("；等待：%1").arg(waiting.join(tr("、")));
        }
    }

    // The alarm is settled whether or not anything is monitored right now.
    const bool newFault = (faults & ~alarmSources) != 0;
    alarmSources |= faults;
    const auto describeAlarmSources = [this, alarmSources]() {
        QStringList reasons;
        if (alarmSources & 1) { reasons << tr("飞控心跳中断"); }
        for (int i = 0; i < _channels.size(); ++i) {
            if (alarmSources & (quint64(1) << (i + 1))) { reasons << tr("%1视频无新帧").arg(_channels[i].name); }
        }
        return reasons.join(tr("；"));
    };
    const auto endAlarm = [this](const QString &text) {
        _alarmActive = false;
        _recoverySince = -1;
        _pendingAcknowledgement = !_acknowledged;
        _alarmText = text;
        _soundPending = false;
        if (_sound) { _sound->stop(); }
        emit eventOccurred(_alarmText);
    };
    if (_alarmActive && !retired.isEmpty() && alarmSources != 0) {
        // A switched-off source is no longer part of the alarm. Say so once, and name only what is left,
        // so that a later recovery does not count it as recovered.
        _alarmText = describeAlarmSources();
        emit eventOccurred(tr("%1已停用，不再监测；仍在告警：%2").arg(retired.join(tr("、")), _alarmText));
    }
    if (faults) {
        _recoverySince = -1;
        _alarmActive = true;
        _pendingAcknowledgement = true;
        _alarmText = describeAlarmSources();
        if (newFault) {
            _acknowledged = false;
            _lastSound = -1;
            emit eventOccurred(tr("异常断联：%1").arg(_alarmText));
        }
    } else if (_alarmActive && !retired.isEmpty() && alarmSources == 0) {
        // Switching the failed source off ends the alarm, but it is not a recovery.
        endAlarm(tr("告警结束：%1已停用，不再监测").arg(retired.join(tr("、"))));
    } else if (_alarmActive && alarmSourcesFresh) {
        if (_recoverySince < 0) { _recoverySince = now; }
        if (now - _recoverySince >= _recoverySeconds * 1000) {
            alarmSources = 0;
            endAlarm(tr("链路已恢复：%1").arg(_alarmText));
        }
    } else {
        _recoverySince = -1;
    }
    _faultMask = _alarmActive ? alarmSources : 0;
    _repeatSound(now);
    _notifyStateIfChanged();
}

void DeepSharkConnectionMonitor::_repeatSound(qint64 now)
{
    if (_alarmActive && !_acknowledged && _soundEnabled
            && (_lastSound < 0 || now - _lastSound >= _repeatSeconds * 1000)) {
        _lastSound = now;
        emit soundRequested();
        if (_live) { _playSound(); }
    }
}

void DeepSharkConnectionMonitor::acknowledge()
{
    if (!attentionRequired()) { return; }
    _acknowledged = true;
    _pendingAcknowledgement = false;
    _soundPending = false;
    if (_sound) { _sound->stop(); }
    emit eventOccurred(tr("用户确认断联提示并静音"));
    emit stateChanged();
}

QString DeepSharkConnectionMonitor::audioStatus() const
{
    if (!_soundEnabled) { return tr("预警声音已关闭"); }
    if (_live && SettingsManager::instance()->appSettings()->audioMuted()->rawValue().toBool()) {
        return tr("应用总声音已静音");
    }
    if (_sound && _sound->status() == QSoundEffect::Error) { return tr("提示音不可用，请检查声音设备"); }
    return tr("请使用声音测试确认实际输出");
}

void DeepSharkConnectionMonitor::_playSound()
{
    if (!_soundEnabled || SettingsManager::instance()->appSettings()->audioMuted()->rawValue().toBool()) {
        return;
    }
    _soundPending = true;
    if (!_sound) {
        _sound = new QSoundEffect(this);
        _sound->setVolume(0.7f);
        connect(_sound, &QSoundEffect::statusChanged, this, [this] {
            emit stateChanged();
            if (_sound->status() == QSoundEffect::Ready && _soundPending) {
                _soundPending = false;
                if (_soundEnabled && !SettingsManager::instance()->appSettings()->audioMuted()->rawValue().toBool()) {
                    _sound->play();
                }
            }
        });
        _sound->setSource(QUrl(_soundType == 1
                                  ? QStringLiteral("qrc:/Custom/audio/connection-alert-hi-low.wav")
                                  : QStringLiteral("qrc:/Custom/audio/connection-alert.wav")));
    } else if (_sound->status() == QSoundEffect::Ready) {
        _soundPending = false;
        _sound->play();
    }
}

void DeepSharkConnectionMonitor::testSound()
{
    if (_live) { _playSound(); }
    emit stateChanged();
}
