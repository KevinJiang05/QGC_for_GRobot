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

#include <cmath>

#ifdef QGC_GST_STREAMING
#include <gst/gstbuffer.h>
#include <gst/gstclock.h>
#include <gst/gstelement.h>
#endif

DeepSharkVideoController::DeepSharkVideoController(QObject *parent)
    : QObject(parent)
{
    _setStatusText(tr("Waiting for RTSP"));
    _frameRateTimer.setInterval(1000);
    connect(&_frameRateTimer, &QTimer::timeout, this, &DeepSharkVideoController::_updateFrameRate);
    _frameRateTimer.start();
}

DeepSharkVideoController::~DeepSharkVideoController()
{
    _frameRateTimer.stop();
    _removeSinkFrameProbe();

    if (_receiver) {
        disconnect(_receiver, nullptr, this, nullptr);
        delete _receiver;
        _receiver = nullptr;
    }

    if (_sink) {
        QGCCorePlugin::instance()->releaseVideoSink(_sink);
        _sink = nullptr;
    }
}

void DeepSharkVideoController::setVideoItem(QQuickItem *videoItem)
{
    if (_videoItem == videoItem) {
        return;
    }

    _videoItem = videoItem;
    emit videoItemChanged();
    _rebuildSink();

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

QString DeepSharkVideoController::resolutionText() const
{
    if (!_videoSize.isValid() || _videoSize.isEmpty()) {
        return QStringLiteral("分辨率: --");
    }

    return QStringLiteral("分辨率: %1x%2").arg(_videoSize.width()).arg(_videoSize.height());
}

QString DeepSharkVideoController::frameRateText() const
{
    if (!_decoding || _frameRate <= 0.0) {
        return QStringLiteral("FPS: --");
    }

    return QStringLiteral("FPS: %1").arg(_frameRate, 0, 'f', 1);
}

quint64 DeepSharkVideoController::frameCount() const
{
#ifdef QGC_GST_STREAMING
    return _sinkFrameCount.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}

int DeepSharkVideoController::estimatedLatencyMs() const
{
    return _estimatedLatencyMs;
}

QString DeepSharkVideoController::latencyText() const
{
    if (_estimatedLatencyMs < 0) {
        return QStringLiteral("Latency: --");
    }

    return QStringLiteral("Latency: %1 ms").arg(_estimatedLatencyMs);
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
#ifdef QGC_GST_STREAMING
    _sinkFrameCount.store(0, std::memory_order_relaxed);
    _sinkLatencyMs.store(-1, std::memory_order_relaxed);
    _lastSinkFrameCount = 0;
    emit frameCountChanged();
#endif
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

    connect(_receiver, &VideoReceiver::timeout, this, [this]() {
        _setStatusText(tr("Stream timeout"));
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

    connect(_receiver, &VideoReceiver::videoSizeChanged, this, [this](const QSize &size) {
        _setVideoSize(size);
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
        _removeSinkFrameProbe();
        _receiver->setSink(nullptr);
        QGCCorePlugin::instance()->releaseVideoSink(_sink);
        _sink = nullptr;
    }

    _receiver->setWidget(_videoItem);
    if (_videoItem) {
        _sink = QGCCorePlugin::instance()->createVideoSink(_videoItem, _receiver);
        _receiver->setSink(_sink);
        _installSinkFrameProbe();
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
    if (!_decoding) {
        _setFrameRate(0.0);
#ifdef QGC_GST_STREAMING
        _sinkLatencyMs.store(-1, std::memory_order_relaxed);
#endif
        if (_estimatedLatencyMs != -1) {
            _estimatedLatencyMs = -1;
            emit latencyChanged();
        }
    }
    emit decodingChanged();
}

void DeepSharkVideoController::_setVideoSize(const QSize &videoSize)
{
    if (_videoSize == videoSize) {
        return;
    }

    _videoSize = videoSize;
    emit videoSizeChanged();
}

void DeepSharkVideoController::_setFrameRate(double frameRate)
{
    if (std::abs(_frameRate - frameRate) < 0.05) {
        return;
    }

    _frameRate = frameRate;
    emit frameRateTextChanged();
}

void DeepSharkVideoController::_updateFrameRate()
{
#ifdef QGC_GST_STREAMING
    const quint64 currentFrameCount = _sinkFrameCount.load(std::memory_order_relaxed);
    if (currentFrameCount != _lastSinkFrameCount) {
        const quint64 frameDelta = currentFrameCount - _lastSinkFrameCount;
        _lastSinkFrameCount = currentFrameCount;
        emit frameCountChanged();
        _setFrameRate(_decoding ? static_cast<double>(frameDelta) : 0.0);
    } else {
        _setFrameRate(0.0);
    }

    const qint64 latencyMs = _sinkLatencyMs.load(std::memory_order_relaxed);
    if (_estimatedLatencyMs != static_cast<int>(latencyMs)) {
        _estimatedLatencyMs = static_cast<int>(latencyMs);
        emit latencyChanged();
    }
#else
    _setFrameRate(0.0);
    if (_estimatedLatencyMs != -1) {
        _estimatedLatencyMs = -1;
        emit latencyChanged();
    }
#endif
}

#ifdef QGC_GST_STREAMING
void DeepSharkVideoController::_installSinkFrameProbe()
{
    if (!_sink || _sinkFrameProbeId != 0) {
        return;
    }

    GstElement *videoSink = GST_ELEMENT(_sink);
    GstPad *sinkPad = gst_element_get_static_pad(videoSink, "sink");
    if (!sinkPad) {
        return;
    }

    _sinkFrameProbeId = gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_BUFFER, _videoSinkFrameProbe, this, nullptr);
    gst_object_unref(sinkPad);
}

void DeepSharkVideoController::_removeSinkFrameProbe()
{
    if (!_sink || _sinkFrameProbeId == 0) {
        _sinkFrameProbeId = 0;
        return;
    }

    GstElement *videoSink = GST_ELEMENT(_sink);
    GstPad *sinkPad = gst_element_get_static_pad(videoSink, "sink");
    if (sinkPad) {
        gst_pad_remove_probe(sinkPad, _sinkFrameProbeId);
        gst_object_unref(sinkPad);
    }

    _sinkFrameProbeId = 0;
}

GstPadProbeReturn DeepSharkVideoController::_videoSinkFrameProbe(GstPad *pad, GstPadProbeInfo *info, gpointer userData)
{
    auto *controller = static_cast<DeepSharkVideoController *>(userData);
    if (controller && (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
        controller->_sinkFrameCount.fetch_add(1, std::memory_order_relaxed);
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        const GstClockTime pts = buffer ? GST_BUFFER_PTS(buffer) : GST_CLOCK_TIME_NONE;
        if (GST_CLOCK_TIME_IS_VALID(pts)) {
            GstElement *sink = GST_ELEMENT(gst_pad_get_parent(pad));
            if (sink) {
                GstClock *clock = gst_element_get_clock(sink);
                const GstClockTime baseTime = gst_element_get_base_time(sink);
                if (clock && GST_CLOCK_TIME_IS_VALID(baseTime)) {
                    const GstClockTime now = gst_clock_get_time(clock);
                    if (GST_CLOCK_TIME_IS_VALID(now) && now >= baseTime) {
                        const GstClockTime runningTime = now - baseTime;
                        if (runningTime >= pts) {
                            const guint64 latencyMs = (runningTime - pts) / GST_MSECOND;
                            if (latencyMs <= 60000) {
                                controller->_sinkLatencyMs.store(static_cast<qint64>(latencyMs), std::memory_order_relaxed);
                            }
                        }
                    }
                }
                if (clock) {
                    gst_object_unref(clock);
                }
                gst_object_unref(sink);
            }
        }
    }

    return GST_PAD_PROBE_OK;
}
#else
void DeepSharkVideoController::_installSinkFrameProbe()
{
}

void DeepSharkVideoController::_removeSinkFrameProbe()
{
}
#endif
