/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "AIDetectionReceiver.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkDatagram>
#include <QtNetwork/QUdpSocket>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr qint64 kMaxDatagramBytes = 32 * 1024;
constexpr qsizetype kMaxDetectionsPerPacket = 64;
constexpr qsizetype kMaxSources = 4;
constexpr qsizetype kMaxSourceIdLength = 64;
constexpr qsizetype kMaxLabelLength = 64;
constexpr double kMaxFrameDimension = 16384.0;
constexpr double kMaxTimestampSkewSeconds = 5.0;
constexpr int kStaleTimeoutMs = 1500;
constexpr int kStaleCheckIntervalMs = 250;

bool finiteJsonNumber(const QJsonValue &value, double &number)
{
    if (!value.isDouble()) {
        return false;
    }
    number = value.toDouble();
    return std::isfinite(number);
}

bool validSourceId(const QString &sourceId)
{
    static const QRegularExpression sourcePattern(QStringLiteral("^[A-Za-z0-9_.-]+$"));
    return sourceId.size() <= kMaxSourceIdLength
        && (sourceId.isEmpty() || sourcePattern.match(sourceId).hasMatch());
}
}

AIDetectionReceiver::AIDetectionReceiver(QObject *parent)
    : QObject(parent)
{
    _statusText = tr("AI 检测已停用");
    _elapsedTimer.start();
    _staleTimer.setInterval(kStaleCheckIntervalMs);
    connect(&_staleTimer, &QTimer::timeout, this, &AIDetectionReceiver::_expireStaleDetections);
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

void AIDetectionReceiver::clearDetections()
{
    _detectionsBySource.clear();
    _lastUpdateMsBySource.clear();
    _setDetections({});
}

void AIDetectionReceiver::_updateSocket()
{
    const bool wasBound = bound();

    _closeSocket();
    _staleTimer.stop();
    clearDetections();

    if (!_enabled) {
        _setStatusText(tr("AI 检测已停用"));
        if (wasBound != bound()) {
            emit boundChanged();
        }
        return;
    }

    _socket = new QUdpSocket(this);
    connect(_socket, &QUdpSocket::readyRead, this, &AIDetectionReceiver::_readPendingDatagrams);

    const bool ok = _socket->bind(QHostAddress::LocalHost, _port, QUdpSocket::DontShareAddress);
    _setStatusText(ok ? tr("正在监听本机 AI 检测 UDP 端口 %1").arg(_port)
                      : tr("AI 检测 UDP 端口 %1 绑定失败：%2").arg(_port).arg(_socket->errorString()));
    if (ok) {
        _staleTimer.start();
    }

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
        const QNetworkDatagram networkDatagram = _socket->receiveDatagram();
        if (!networkDatagram.senderAddress().isLoopback()) {
            _setStatusText(tr("已拒绝非本机 AI 检测数据包"));
            continue;
        }

        const QByteArray datagram = networkDatagram.data();
        if (datagram.size() > kMaxDatagramBytes) {
            _setStatusText(tr("已拒绝过大的 AI 检测数据包"));
            continue;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(datagram, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            _setStatusText(tr("AI 检测 JSON 无效"));
            continue;
        }

        const QJsonObject root = document.object();
        const QJsonValue detectionsValue = root.value(QStringLiteral("detections"));
        if (!detectionsValue.isArray()) {
            _setStatusText(tr("AI 检测结果列表无效"));
            continue;
        }

        const QJsonArray rawDetections = detectionsValue.toArray();
        if (rawDetections.size() > kMaxDetectionsPerPacket) {
            _setStatusText(tr("已拒绝目标框数量过多的 AI 检测数据包"));
            continue;
        }

        const QJsonValue sourceIdValue = root.value(QStringLiteral("source_id"));
        if (!sourceIdValue.isUndefined() && !sourceIdValue.isString()) {
            _setStatusText(tr("AI 检测来源 ID 无效"));
            continue;
        }
        const QString sourceId = sourceIdValue.toString().trimmed();
        if (!validSourceId(sourceId)) {
            _setStatusText(tr("AI 检测来源 ID 无效"));
            continue;
        }
        if (!_detectionsBySource.contains(sourceId) && _detectionsBySource.size() >= kMaxSources) {
            _setStatusText(tr("已拒绝 AI 检测数据包：来源数量达到上限"));
            continue;
        }

        const QJsonValue timestampValue = root.value(QStringLiteral("timestamp"));
        if (!timestampValue.isUndefined()) {
            double timestamp = 0.0;
            if (!finiteJsonNumber(timestampValue, timestamp)) {
                _setStatusText(tr("AI 检测时间戳无效"));
                continue;
            }
            const double nowSeconds = QDateTime::currentMSecsSinceEpoch() / 1000.0;
            if (std::abs(nowSeconds - timestamp) > kMaxTimestampSkewSeconds) {
                _setStatusText(tr("已拒绝过期的 AI 检测数据包"));
                continue;
            }
        }

        double frameWidth = 0.0;
        double frameHeight = 0.0;
        const QJsonValue frameWidthValue = root.value(QStringLiteral("frame_width"));
        const QJsonValue frameHeightValue = root.value(QStringLiteral("frame_height"));
        if (!frameWidthValue.isUndefined()
            && (!finiteJsonNumber(frameWidthValue, frameWidth) || frameWidth <= 0.0 || frameWidth > kMaxFrameDimension)) {
            _setStatusText(tr("AI 检测画面宽度无效"));
            continue;
        }
        if (!frameHeightValue.isUndefined()
            && (!finiteJsonNumber(frameHeightValue, frameHeight) || frameHeight <= 0.0 || frameHeight > kMaxFrameDimension)) {
            _setStatusText(tr("AI 检测画面高度无效"));
            continue;
        }

        QVariantList detections;
        detections.reserve(rawDetections.size());

        for (const QJsonValue &value : rawDetections) {
            if (!value.isObject()) {
                continue;
            }

            const QVariantMap detection = _parseDetection(value.toObject(), frameWidth, frameHeight);
            if (!detection.isEmpty()) {
                QVariantMap sourceDetection = detection;
                sourceDetection.insert(QStringLiteral("source_id"), sourceId);
                detections.append(sourceDetection);
            }
        }

        _setDetectionsForSource(sourceId, detections);
        _setStatusText(tr("已收到 %1 个 AI 检测目标").arg(detections.size()));
    }
}

void AIDetectionReceiver::_expireStaleDetections()
{
    const qint64 now = _elapsedTimer.elapsed();
    bool removed = false;
    for (auto it = _lastUpdateMsBySource.begin(); it != _lastUpdateMsBySource.end();) {
        if (now - it.value() > kStaleTimeoutMs) {
            _detectionsBySource.remove(it.key());
            it = _lastUpdateMsBySource.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        _rebuildDetections();
        _setStatusText(tr("AI 检测结果已过期"));
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
    if (_detections == detections) {
        return;
    }
    _detections = detections;
    emit detectionsChanged();
}

void AIDetectionReceiver::_setDetectionsForSource(const QString &sourceId, const QVariantList &detections)
{
    _detectionsBySource.insert(sourceId, detections);
    _lastUpdateMsBySource.insert(sourceId, _elapsedTimer.elapsed());
    _rebuildDetections();
}

void AIDetectionReceiver::_rebuildDetections()
{
    QVariantList merged;
    for (const QVariantList &sourceDetections : std::as_const(_detectionsBySource)) {
        for (const QVariant &detection : sourceDetections) {
            merged.append(detection);
        }
    }
    _setDetections(merged);
}

QVariantMap AIDetectionReceiver::_parseDetection(const QJsonObject &detection, double frameWidth, double frameHeight) const
{
    double x = -1.0;
    double y = -1.0;
    double w = -1.0;
    double h = -1.0;
    const bool hasNormalizedBox = finiteJsonNumber(detection.value(QStringLiteral("x")), x)
        && finiteJsonNumber(detection.value(QStringLiteral("y")), y)
        && finiteJsonNumber(detection.value(QStringLiteral("w")), w)
        && finiteJsonNumber(detection.value(QStringLiteral("h")), h);

    if (!hasNormalizedBox) {
        const QJsonValue bboxValue = detection.value(QStringLiteral("bbox"));
        if (!bboxValue.isArray() || frameWidth <= 0.0 || frameHeight <= 0.0) {
            return {};
        }
        const QJsonArray bbox = bboxValue.toArray();
        if (bbox.size() != 4) {
            return {};
        }

        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        if (!finiteJsonNumber(bbox.at(0), left)
            || !finiteJsonNumber(bbox.at(1), top)
            || !finiteJsonNumber(bbox.at(2), right)
            || !finiteJsonNumber(bbox.at(3), bottom)) {
            return {};
        }

        x = left / frameWidth;
        y = top / frameHeight;
        w = (right - left) / frameWidth;
        h = (bottom - top) / frameHeight;
    }

    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h)
        || x < 0.0 || y < 0.0 || w <= 0.0 || h <= 0.0) {
        return {};
    }

    x = _clampUnit(x);
    y = _clampUnit(y);
    w = std::min(_clampUnit(w), 1.0 - x);
    h = std::min(_clampUnit(h), 1.0 - y);
    if (w <= 0.0 || h <= 0.0) {
        return {};
    }

    const QJsonValue confidenceValue = detection.value(QStringLiteral("confidence"));
    double confidence = 0.0;
    if (!confidenceValue.isUndefined() && !finiteJsonNumber(confidenceValue, confidence)) {
        return {};
    }

    const QJsonValue labelValue = detection.value(QStringLiteral("label"));
    if (!labelValue.isUndefined() && !labelValue.isString()) {
        return {};
    }
    const QString label = labelValue.toString(QStringLiteral("target")).left(kMaxLabelLength);

    QVariantMap parsed;
    parsed.insert(QStringLiteral("x"), x);
    parsed.insert(QStringLiteral("y"), y);
    parsed.insert(QStringLiteral("w"), w);
    parsed.insert(QStringLiteral("h"), h);
    parsed.insert(QStringLiteral("label"), label);
    parsed.insert(QStringLiteral("confidence"), _clampUnit(confidence));

    return parsed;
}

double AIDetectionReceiver::_clampUnit(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::max(0.0, std::min(1.0, value));
}
