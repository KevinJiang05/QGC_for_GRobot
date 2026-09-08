/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QElapsedTimer>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtNetwork/QHostAddress>

class QJsonObject;
class QUdpSocket;

class AIDetectionReceiver : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool bound READ bound NOTIFY boundChanged)
    Q_PROPERTY(quint16 port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QVariantList detections READ detections NOTIFY detectionsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit AIDetectionReceiver(QObject *parent = nullptr);
    ~AIDetectionReceiver();

    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled);

    bool bound() const;

    quint16 port() const { return _port; }
    void setPort(quint16 port);

    QVariantList detections() const { return _detections; }
    QString statusText() const { return _statusText; }

    Q_INVOKABLE QVariantList detectionsForSource(const QString &sourceId) const;
    Q_INVOKABLE void clearDetections();

signals:
    void enabledChanged();
    void boundChanged();
    void portChanged();
    void detectionsChanged();
    void statusTextChanged();

private slots:
    void _readPendingDatagrams();
    void _expireStaleDetections();

private:
    void _updateSocket();
    void _closeSocket();
    void _setStatusText(const QString &statusText);
    void _setDetections(const QVariantList &detections);
    void _setDetectionsForSource(const QString &sourceId, const QVariantList &detections);
    void _rebuildDetections();
    QVariantMap _parseDetection(const QJsonObject &detection, double frameWidth, double frameHeight) const;
    static double _clampUnit(double value);

    QUdpSocket *_socket = nullptr;
    bool _enabled = false;
    quint16 _port = 57610;
    QVariantList _detections;
    QHash<QString, QVariantList> _detectionsBySource;
    QHash<QString, qint64> _lastUpdateMsBySource;
    QElapsedTimer _elapsedTimer;
    QTimer _staleTimer;
    QString _statusText;
};
