/****************************************************************************
 *
 * DeepShark single-stream video controller.
 *
 ****************************************************************************/

#include "DeepSharkVideoController.h"

#include "QGCCorePlugin.h"
#include "VideoReceiver.h"

#include <QtCore/QTimer>
#include <QtQuick/QQuickItem>

DeepSharkVideoController::DeepSharkVideoController(QObject *parent)
    : QObject(parent)
{
    _setStatusText(tr("Waiting for RTSP"));
}

DeepSharkVideoController::~DeepSharkVideoController()
{
    stop();
    if (_receiver && _sink) {
        QGCCorePlugin::instance()->releaseVideoSink(_sink);
    }
    _sink = nullptr;
}

void DeepSharkVideoController::setVideoItem(QQuickItem *videoItem)
{
    if (_videoItem == videoItem) {
        return;
    }

    _videoItem = videoItem;
    emit videoItemChanged();
    _rebuildSink();

    if (_autoStart) {
        if (_receiver && _receiver->started()) {
            stop();
            QTimer::singleShot(1200, this, &DeepSharkVideoController::start);
        } else {
            QTimer::singleShot(250, this, &DeepSharkVideoController::start);
        }
    }
}

void DeepSharkVideoController::setReceiverName(const QString &receiverName)
{
    const QString name = receiverName.isEmpty() ? QStringLiteral("deepSharkVideo") : receiverName;
    if (_receiverName == name) {
        return;
    }

    _receiverName = name;
    emit receiverNameChanged();

    if (_receiver) {
        _receiver->setName(_receiverName);
    }
}

void DeepSharkVideoController::setUri(const QString &uri)
{
    if (_uri == uri) {
        return;
    }

    _uri = uri;
    emit uriChanged();

    if (_receiver) {
        _receiver->setUri(_uri);
    }

    if (_autoStart) {
        if (_receiver && _receiver->started()) {
            stop();
            QTimer::singleShot(1200, this, &DeepSharkVideoController::start);
        } else {
            QTimer::singleShot(250, this, &DeepSharkVideoController::start);
        }
    }
}

void DeepSharkVideoController::setAutoStart(bool autoStart)
{
    if (_autoStart == autoStart) {
        return;
    }

    _autoStart = autoStart;
    emit autoStartChanged();

    if (_autoStart) {
        QTimer::singleShot(250, this, &DeepSharkVideoController::start);
    } else {
        stop();
    }
}

void DeepSharkVideoController::setLowLatency(bool lowLatency)
{
    if (_lowLatency == lowLatency) {
        return;
    }

    _lowLatency = lowLatency;
    emit lowLatencyChanged();

    if (_receiver) {
        _receiver->setLowLatency(_lowLatency);
    }
}

void DeepSharkVideoController::start()
{
    _ensureReceiver();

    if (!_receiver || !_videoItem || !_sink) {
        _setStatusText(tr("Video sink not ready"));
        return;
    }

    if (_uri.isEmpty()) {
        _setStatusText(tr("Set RTSP URL in Video Settings"));
        return;
    }

    if (_receiver->started()) {
        return;
    }

    _startAttempts++;
    emit startAttemptsChanged();
    _setStatusText(tr("Connecting"));
    _receiver->setUri(_uri);
    _receiver->setLowLatency(_lowLatency);
    _receiver->start(8);
}

void DeepSharkVideoController::stop()
{
    if (_receiver && _receiver->started()) {
        _receiver->stop();
    }
}

void DeepSharkVideoController::_ensureReceiver()
{
    if (_receiver) {
        return;
    }

    _receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!_receiver) {
        _setStatusText(tr("Video receiver unavailable"));
        return;
    }

    _receiver->setName(_receiverName);
    _receiver->setLowLatency(_lowLatency);
    _receiver->setUri(_uri);

    connect(_receiver, &VideoReceiver::onStartComplete, this, [this](VideoReceiver::STATUS status) {
        if (status == VideoReceiver::STATUS_OK) {
            _receiver->setStarted(true);
            _setStatusText(tr("Streaming"));
            if (_sink) {
                _receiver->startDecoding(_sink);
            }
        } else {
            _setStatusText(tr("Start failed: %1").arg(status));
        }
    });

    connect(_receiver, &VideoReceiver::onStopComplete, this, [this](VideoReceiver::STATUS) {
        _receiver->setStarted(false);
        _setStreaming(false);
        _setDecoding(false);
        _setStatusText(tr("Stopped"));
    });

    connect(_receiver, &VideoReceiver::onStartDecodingComplete, this, [this](VideoReceiver::STATUS status) {
        if (status == VideoReceiver::STATUS_OK) {
            _setStatusText(tr("Playing"));
        } else {
            _setStatusText(tr("Decode failed: %1").arg(status));
        }
    });

    connect(_receiver, &VideoReceiver::streamingChanged, this, [this](bool active) {
        _setStreaming(active);
        if (active && !_decoding) {
            _setStatusText(tr("Streaming"));
        }
    });

    connect(_receiver, &VideoReceiver::decodingChanged, this, [this](bool active) {
        _setDecoding(active);
        _setStatusText(active ? tr("Playing") : (_streaming ? tr("Streaming") : tr("Waiting for RTSP")));
    });

    _rebuildSink();
}

void DeepSharkVideoController::_rebuildSink()
{
    _ensureReceiver();
    if (!_receiver) {
        return;
    }

    if (_sink) {
        QGCCorePlugin::instance()->releaseVideoSink(_sink);
        _sink = nullptr;
    }

    _receiver->setWidget(_videoItem);
    if (_videoItem) {
        _sink = QGCCorePlugin::instance()->createVideoSink(_videoItem, _receiver);
        _receiver->setSink(_sink);
    }
}

void DeepSharkVideoController::_setStatusText(const QString &statusText)
{
    if (_statusText == statusText) {
        return;
    }

    _statusText = statusText;
    emit statusTextChanged();
}

void DeepSharkVideoController::_setStreaming(bool streaming)
{
    if (_streaming == streaming) {
        return;
    }

    _streaming = streaming;
    emit streamingChanged();
}

void DeepSharkVideoController::_setDecoding(bool decoding)
{
    if (_decoding == decoding) {
        return;
    }

    _decoding = decoding;
    emit decodingChanged();
}
