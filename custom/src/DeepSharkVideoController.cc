/****************************************************************************
 *
 * DeepShark single-stream video controller.
 *
 ****************************************************************************/

#include "DeepSharkVideoController.h"

#include <QtCore/QTimer>
#include <QtQuick/QQuickItem>
#include <algorithm>
#include <cmath>

#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "VideoBackend.h"
#include "VideoReceiver.h"

#ifdef QGC_GST_STREAMING
#include <gst/gstbuffer.h>
#include <gst/gstclock.h>
#include <gst/gstelement.h>
#endif

QGC_LOGGING_CATEGORY(DeepSharkVideoControllerLog, "qgc.deepshark.videocontroller")

DeepSharkVideoController::DeepSharkVideoController(QObject *parent)
    : QObject(parent)
{
    _setStatusText(tr("Waiting for RTSP"));
    _startTimer.setSingleShot(true);
    connect(&_startTimer, &QTimer::timeout, this, &DeepSharkVideoController::start);
    _frameRateTimer.setInterval(1000);
    connect(&_frameRateTimer, &QTimer::timeout, this, &DeepSharkVideoController::_updateFrameRate);
}

DeepSharkVideoController::~DeepSharkVideoController()
{
    _startTimer.stop();
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
    if (_receiver && (_receiver->started() || _startPending)) {
        if (_videoItem && _autoStart) {
            restart(250);
        }
        return;
    }
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

    if (_uri.isEmpty()) {
        stop();
        return;
    }

    if (_autoStart) {
        restart((_receiver && (_receiver->started() || _startPending)) ? 1200 : 250);
    } else {
        _startTimer.stop();
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
        if (_stopRequested && _receiver && (_receiver->started() || _startPending)) {
            restart(250);
        } else {
            _stopRequested = false;
            _scheduleStart(250);
        }
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
        return tr("分辨率：--");
    }

    return tr("分辨率：%1x%2").arg(_videoSize.width()).arg(_videoSize.height());
}

QString DeepSharkVideoController::frameRateText() const
{
    if (!_decoding || _frameRate <= 0.0) {
        return tr("FPS：--");
    }

    return tr("FPS：%1").arg(_frameRate, 0, 'f', 1);
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
        return tr("延迟：--");
    }

    return tr("延迟：%1 ms").arg(_estimatedLatencyMs);
}

void DeepSharkVideoController::start()
{
    _startTimer.stop();
    if (!_autoStart) {
        _setStatusText(tr("Stopped"));
        return;
    }

    _ensureReceiver();

    if (_receiver && _videoItem && !_sink) {
        _rebuildSink();
    }

    if (!_receiver || !_videoItem || !_sink) {
        _reportFailure(tr("视频输出尚未就绪"));
        return;
    }

    if (_uri.isEmpty()) {
        _reportFailure(tr("请先在视频设置中填写 RTSP URL"));
        return;
    }

    if (_receiver->started() || _startPending) {
        return;
    }

    _stopRequested = false;
    _restartRequested = false;
    _startPending = true;
    _startAttempts++;
    _firstFrameLogged = false;
    _startupElapsedTimer.restart();
    qCDebug(DeepSharkVideoControllerLog) << "Start attempt" << _receiverName << "attempt=" << _startAttempts;
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
    _frameRateTimer.start();
    _receiver->start(8);
}

void DeepSharkVideoController::stop()
{
    _startTimer.stop();
    _stopRequested = true;
    _restartRequested = false;
    _restartNeedsSinkRebuild = false;
    if (_receiver && _receiver->started()) {
        _receiver->stop();
    } else if (!_startPending) {
        _setStreaming(false);
        _setDecoding(false);
        _setStatusText(tr("Stopped"));
        _releaseReceiver();
    }
}

void DeepSharkVideoController::restart(int delayMs)
{
    _startTimer.stop();
    if (!_autoStart || _uri.isEmpty()) {
        _restartRequested = false;
        return;
    }

    _stopRequested = false;
    _restartDelayMs = std::clamp(delayMs, 0, 30000);
    _restartNeedsSinkRebuild = true;
    if (_receiver && (_receiver->started() || _startPending)) {
        _restartRequested = true;
        _setStatusText(tr("Reconnecting"));
        if (_receiver->started()) {
            _receiver->stop();
        }
        return;
    }

    _restartRequested = false;
    _prepareRestart(_restartDelayMs);
}

void DeepSharkVideoController::_prepareRestart(int delayMs)
{
    if (_restartNeedsSinkRebuild) {
        _rebuildSink();
        _restartNeedsSinkRebuild = false;
    }
    _setStatusText(tr("Reconnecting"));
    _scheduleStart(delayMs);
}

void DeepSharkVideoController::_scheduleStart(int delayMs)
{
    _startTimer.stop();
    if (!_autoStart || _uri.isEmpty()) {
        return;
    }
    _startTimer.start(std::clamp(delayMs, 0, 30000));
}

void DeepSharkVideoController::_releaseReceiver()
{
    if (!_receiver || _receiver->started() || _startPending || _restartRequested || _autoStart) {
        return;
    }
    disconnect(_receiver, nullptr, this, nullptr);
    _removeSinkFrameProbe();
    _receiver->setSink(nullptr);
    if (_sink) {
        QGCCorePlugin::instance()->releaseVideoSink(_sink);
        _sink = nullptr;
    }
    _frameRateTimer.stop();
    _receiver->deleteLater();
    _receiver = nullptr;
}

void DeepSharkVideoController::_ensureReceiver()
{
    if (_receiver) {
        return;
    }

    _receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
    if (!_receiver) {
        _reportFailure(tr("视频接收器不可用"));
        return;
    }

    _receiver->setName(_receiverName);
    // ConnectionMonitor/VideoTile own reconnect scheduling for custom streams.
    _receiver->setAutoReconnect(false);
    _receiver->setLowLatency(_lowLatency);
    _receiver->setUri(_uri);

    const QPointer<VideoReceiver> receiver = _receiver;

    connect(_receiver, &VideoReceiver::onStartComplete, this, [this, receiver](VideoReceiver::STATUS status) {
        if (!receiver || _receiver != receiver) {
            return;
        }
        _startPending = false;
        if (status == VideoReceiver::STATUS_OK) {
            _receiver->setStarted(true);
            if (_stopRequested || !_autoStart || _restartRequested) {
                _receiver->stop();
                return;
            }
            _setStatusText(tr("Streaming"));
            if (_sink) {
                _receiver->startDecoding(_sink);
            }
        } else {
            if (_restartRequested && _autoStart) {
                const int delayMs = _restartDelayMs;
                _restartRequested = false;
                _prepareRestart(delayMs);
            } else if (_stopRequested || !_autoStart) {
                _setStatusText(tr("Stopped"));
                _releaseReceiver();
            } else {
                _reportFailure(tr("视频启动失败：%1").arg(status));
            }
        }
    });

    connect(_receiver, &VideoReceiver::onStopComplete, this, [this, receiver](VideoReceiver::STATUS) {
        if (!receiver || _receiver != receiver) {
            return;
        }
        _receiver->setStarted(false);
        _startPending = false;
        _setStreaming(false);
        _setDecoding(false);
        if (_restartRequested && _autoStart && !_stopRequested) {
            const int delayMs = _restartDelayMs;
            _restartRequested = false;
            _prepareRestart(delayMs);
        } else {
            _restartRequested = false;
            _restartNeedsSinkRebuild = false;
            _stopRequested = false;
            _setStatusText(tr("Stopped"));
            _releaseReceiver();
        }
    });

    connect(_receiver, &VideoReceiver::onStartDecodingComplete, this, [this, receiver](VideoReceiver::STATUS status) {
        if (!receiver || _receiver != receiver) {
            return;
        }
        if (status == VideoReceiver::STATUS_OK) {
            _setStatusText(tr("Playing"));
        } else {
            _reportFailure(tr("视频解码失败：%1").arg(status));
        }
    });

    connect(_receiver, &VideoReceiver::timeout, this, [this, receiver]() {
        if (!receiver || _receiver != receiver) {
            return;
        }
        _reportFailure(tr("视频流超时"));
    });

    connect(_receiver, &VideoReceiver::streamingChanged, this, [this, receiver](bool active) {
        if (!receiver || _receiver != receiver) {
            return;
        }
        _setStreaming(active);
        if (active && !_decoding) {
            _setStatusText(tr("Streaming"));
        }
    });

    connect(_receiver, &VideoReceiver::decodingChanged, this, [this, receiver](bool active) {
        if (!receiver || _receiver != receiver) {
            return;
        }
        _setDecoding(active);
        _setStatusText(active ? tr("Playing") : (_streaming ? tr("Streaming") : tr("Waiting for RTSP")));
    });

    connect(_receiver, &VideoReceiver::videoSizeChanged, this, [this, receiver](const QSize &size) {
        if (!receiver || _receiver != receiver) {
            return;
        }
        _setVideoSize(size);
    });

}

void DeepSharkVideoController::_rebuildSink()
{
    if (!_videoItem) {
        // startDecoding may still be queued on the receiver's worker. Keep its
        // sink alive until stop completes, even if QML clears the output first.
        if (_receiver && (_receiver->started() || _startPending)) {
            return;
        }
        if (_receiver && _sink) {
            _removeSinkFrameProbe();
            _receiver->setSink(nullptr);
            QGCCorePlugin::instance()->releaseVideoSink(_sink);
            _sink = nullptr;
        }
        return;
    }

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
        VideoBackend::attachSink(_receiver, _sink, _videoItem);
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

void DeepSharkVideoController::_reportFailure(const QString &message)
{
    _setStatusText(message);
    emit failure(message);
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
    if (_decoding && !_firstFrameLogged && _startupElapsedTimer.isValid()) {
        _firstFrameLogged = true;
        qCDebug(DeepSharkVideoControllerLog) << "First decoded frame" << _receiverName
                                            << "attempt=" << _startAttempts
                                            << "elapsedMs=" << _startupElapsedTimer.elapsed();
    }
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
        controller->_totalFrameCount.fetch_add(1, std::memory_order_relaxed);
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
