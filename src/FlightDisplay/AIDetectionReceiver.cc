/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AIDetectionReceiver.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QUdpSocket>

#include <algorithm>
#include <utility>

AIDetectionReceiver::AIDetectionReceiver(QObject *parent)
    : QObject(parent)
{
    _statusText = QStringLiteral("AI detection disabled");
}

AIDetectionReceiver::~AIDetectionReceiver()
{
    _closeSocket();
}

void AIDetectionReceiver::setEnabled(bool enabled)
{
    if (_enabled == enabled) {
        return;
    }

    _enabled = enabled;
    emit enabledChanged();
    _updateSocket();
}

bool AIDetectionReceiver::bound() const
{
    return _socket && (_socket->state() == QUdpSocket::BoundState);
}

void AIDetectionReceiver::setPort(quint16 port)
{
    if (_port == port) {
        return;
    }

    _port = port;
    emit portChanged();
    _updateSocket();
}

QVariantList AIDetectionReceiver::detectionsForSource(const QString &sourceId) const
{
    QVariantList filtered;
    for (const QVariant &value : _detections) {
        const QVariantMap detection = value.toMap();
        if (detection.value(QStringLiteral("source_id")).toString() == sourceId) {
            filtered.append(value);
        }
    }
    return filtered;
}

void AIDetectionReceiver::_updateSocket()
{
    const bool wasBound = bound();

    _closeSocket();

    if (!_enabled) {
        _setStatusText(QStringLiteral("AI detection disabled"));
        _setDetections({});
        _detectionsBySource.clear();
        if (wasBound != bound()) {
            emit boundChanged();
        }
        return;
    }

    _socket = new QUdpSocket(this);
    connect(_socket, &QUdpSocket::readyRead, this, &AIDetectionReceiver::_readPendingDatagrams);

    const bool ok = _socket->bind(QHostAddress::AnyIPv4, _port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    _setStatusText(ok ? QStringLiteral("Listening for AI detections on UDP %1").arg(_port)
                      : QStringLiteral("AI detection UDP bind failed on %1: %2").arg(_port).arg(_socket->errorString()));

    if (wasBound != bound()) {
        emit boundChanged();
    }
}

void AIDetectionReceiver::_closeSocket()
{
    if (!_socket) {
        return;
    }

    disconnect(_socket, nullptr, this, nullptr);
    _socket->close();
    delete _socket;
    _socket = nullptr;
}

void AIDetectionReceiver::_readPendingDatagrams()
{
    while (_socket && _socket->hasPendingDatagrams()) {
        const QByteArray datagram = _socket->receiveDatagram().data();
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(datagram, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            _setStatusText(QStringLiteral("Invalid AI detection JSON"));
            continue;
        }

        const QJsonObject root = document.object();
        const QJsonArray rawDetections = root.value(QStringLiteral("detections")).toArray();
        const double frameWidth = root.value(QStringLiteral("frame_width")).toDouble();
        const double frameHeight = root.value(QStringLiteral("frame_height")).toDouble();
        const QString sourceId = root.value(QStringLiteral("source_id")).toString();

        QVariantList detections;
        detections.reserve(rawDetections.size());

        for (const QJsonValue &value : rawDetections) {
            if (!value.isObject()) {
                continue;
            }

            const QVariantMap detection = _parseDetection(value.toObject().toVariantMap(), frameWidth, frameHeight);
            if (!detection.isEmpty()) {
                QVariantMap sourceDetection = detection;
                if (!sourceId.isEmpty() && sourceDetection.value(QStringLiteral("source_id")).toString().isEmpty()) {
                    sourceDetection.insert(QStringLiteral("source_id"), sourceId);
                }
                detections.append(sourceDetection);
            }
        }

        if (sourceId.isEmpty()) {
            _detectionsBySource.clear();
            _setDetections(detections);
        } else {
            _setDetectionsForSource(sourceId, detections);
        }
        _setStatusText(QStringLiteral("Received %1 AI detections").arg(detections.size()));
    }
}

void AIDetectionReceiver::_setStatusText(const QString &statusText)
{
    if (_statusText == statusText) {
        return;
    }

    _statusText = statusText;
    emit statusTextChanged();
}

void AIDetectionReceiver::_setDetections(const QVariantList &detections)
{
    _detections = detections;
    emit detectionsChanged();
}

void AIDetectionReceiver::_setDetectionsForSource(const QString &sourceId, const QVariantList &detections)
{
    _detectionsBySource.insert(sourceId, detections);

    QVariantList merged;
    for (const QVariantList &sourceDetections : std::as_const(_detectionsBySource)) {
        for (const QVariant &detection : sourceDetections) {
            merged.append(detection);
        }
    }
    _setDetections(merged);
}

QVariantMap AIDetectionReceiver::_parseDetection(const QVariantMap &detection, double frameWidth, double frameHeight) const
{
    QVariantMap parsed;

    const QVariant xValue = detection.value(QStringLiteral("x"));
    const QVariant yValue = detection.value(QStringLiteral("y"));
    const QVariant wValue = detection.value(QStringLiteral("w"));
    const QVariant hValue = detection.value(QStringLiteral("h"));

    double x = xValue.isValid() ? xValue.toDouble() : -1.0;
    double y = yValue.isValid() ? yValue.toDouble() : -1.0;
    double w = wValue.isValid() ? wValue.toDouble() : -1.0;
    double h = hValue.isValid() ? hValue.toDouble() : -1.0;

    const QVariantList bbox = detection.value(QStringLiteral("bbox")).toList();
    if ((x < 0.0 || y < 0.0 || w <= 0.0 || h <= 0.0) && bbox.size() == 4 && frameWidth > 0.0 && frameHeight > 0.0) {
        const double left = bbox.at(0).toDouble();
        const double top = bbox.at(1).toDouble();
        const double right = bbox.at(2).toDouble();
        const double bottom = bbox.at(3).toDouble();

        x = left / frameWidth;
        y = top / frameHeight;
        w = (right - left) / frameWidth;
        h = (bottom - top) / frameHeight;
    }

    if (x < 0.0 || y < 0.0 || w <= 0.0 || h <= 0.0) {
        return parsed;
    }

    parsed.insert(QStringLiteral("x"), _clampUnit(x));
    parsed.insert(QStringLiteral("y"), _clampUnit(y));
    parsed.insert(QStringLiteral("w"), _clampUnit(w));
    parsed.insert(QStringLiteral("h"), _clampUnit(h));
    parsed.insert(QStringLiteral("label"), detection.value(QStringLiteral("label"), QStringLiteral("target")).toString());
    parsed.insert(QStringLiteral("confidence"), _clampUnit(detection.value(QStringLiteral("confidence"), 0.0).toDouble()));
    parsed.insert(QStringLiteral("source_id"), detection.value(QStringLiteral("source_id")).toString());

    return parsed;
}

double AIDetectionReceiver::_clampUnit(double value)
{
    return std::max(0.0, std::min(1.0, value));
}
