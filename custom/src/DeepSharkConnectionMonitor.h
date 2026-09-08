#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVector>

class DeepSharkVideoController;
class Vehicle;
class QSettings;
class QSoundEffect;
class DeepSharkConnectionMonitorTest;

// Application-owned monitor: hiding/reloading an overlay must not silence it.
class DeepSharkConnectionMonitor : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("DeepSharkVideoController.h")
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool soundEnabled READ soundEnabled WRITE setSoundEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int soundType READ soundType WRITE setSoundType NOTIFY settingsChanged)
    Q_PROPERTY(int videoTimeout READ videoTimeout WRITE setVideoTimeout NOTIFY settingsChanged)
    Q_PROPERTY(int heartbeatTimeout READ heartbeatTimeout WRITE setHeartbeatTimeout NOTIFY settingsChanged)
    Q_PROPERTY(int readySeconds READ readySeconds WRITE setReadySeconds NOTIFY settingsChanged)
    Q_PROPERTY(int recoverySeconds READ recoverySeconds WRITE setRecoverySeconds NOTIFY settingsChanged)
    Q_PROPERTY(int repeatSeconds READ repeatSeconds WRITE setRepeatSeconds NOTIFY settingsChanged)
    Q_PROPERTY(bool monitoring READ monitoring NOTIFY stateChanged)
    Q_PROPERTY(bool alarmActive READ alarmActive NOTIFY stateChanged)
    Q_PROPERTY(bool attentionRequired READ attentionRequired NOTIFY stateChanged)
    Q_PROPERTY(bool acknowledged READ acknowledged NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString alarmText READ alarmText NOTIFY stateChanged)
    Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY stateChanged)

public:
    explicit DeepSharkConnectionMonitor(QObject *parent = nullptr, bool live = true);
    static DeepSharkConnectionMonitor *instance();
    void start();
    bool enabled() const { return _enabled; }
    bool soundEnabled() const { return _soundEnabled; }
    int soundType() const { return _soundType; }
    int videoTimeout() const { return _videoTimeout; }
    int heartbeatTimeout() const { return _heartbeatTimeout; }
    int readySeconds() const { return _readySeconds; }
    int recoverySeconds() const { return _recoverySeconds; }
    int repeatSeconds() const { return _repeatSeconds; }
    bool monitoring() const { return _armed; }
    bool alarmActive() const { return _alarmActive; }
    bool attentionRequired() const { return _alarmActive || _pendingAcknowledgement; }
    bool acknowledged() const { return _acknowledged; }
    QString statusText() const { return _statusText; }
    QString alarmText() const { return _alarmText; }
    QString audioStatus() const;
    void setEnabled(bool value);
    void setSoundEnabled(bool value);
    void setSoundType(int value);
    void setVideoTimeout(int value);
    void setHeartbeatTimeout(int value);
    void setReadySeconds(int value);
    void setRecoverySeconds(int value);
    void setRepeatSeconds(int value);
    Q_INVOKABLE void track(DeepSharkVideoController *controller);
    Q_INVOKABLE void manualReconnect(DeepSharkVideoController *controller);
    Q_INVOKABLE void prepareLinkDisconnect(QObject *link);
    Q_INVOKABLE void acknowledge();
    Q_INVOKABLE void testSound();

signals:
    void settingsChanged();
    void stateChanged();
    void eventOccurred(const QString &message);
    void soundRequested();

private:
    friend class DeepSharkConnectionMonitorTest;
    struct Channel {
        QPointer<DeepSharkVideoController> controller;
        QString name;
        QString id;
        bool expected = false;
        quint64 count = 0;
        qint64 lastFrame = -1;
        qint64 graceUntil = -1;
    };
    void _tick();
    void _evaluate(qint64 now);
    void _setVehicle(Vehicle *vehicle);
    void _plannedDisconnect();
    void _resetReadiness();
    void _loadSettings(QSettings &settings);
    void _saveSettings(QSettings &settings) const;
    void _persist();
    void _playSound();
    void _repeatSound(qint64 now);
    void _setThreshold(int &field, int value, int low, int high);
    QElapsedTimer _clock;
    QTimer _timer;
    QVector<Channel> _channels;
    QPointer<Vehicle> _vehicle;
    QSoundEffect *_sound = nullptr;
    bool _soundPending = false;
    bool _live = true;
    bool _started = false;
    bool _enabled = true;
    bool _soundEnabled = true;
    int _soundType = 0;
    int _videoTimeout = 3;
    int _heartbeatTimeout = 4;
    int _readySeconds = 10;
    int _recoverySeconds = 5;
    int _repeatSeconds = 3;
    bool _flightPresent = false;
    int _vehicleId = -1;
    qint64 _lastHeartbeat = -1;
    qint64 _lastTick = -1;
    qint64 _readySince = -1;
    qint64 _recoverySince = -1;
    qint64 _lastSound = -1;
    bool _armed = false;
    bool _alarmActive = false;
    bool _pendingAcknowledgement = false;
    bool _acknowledged = false;
    quint64 _faultMask = 0;
    QString _statusText;
    QString _alarmText;
};
