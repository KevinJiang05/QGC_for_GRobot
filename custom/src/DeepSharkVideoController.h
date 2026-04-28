/****************************************************************************
 *
 * DeepShark single-stream video controller.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QSize>
#include <QtCore/QTimer>
#include <QtQuick/QQuickItem>

#include <atomic>

#ifdef QGC_GST_STREAMING
#include <gst/gstpad.h>
#endif

class VideoReceiver;

class DeepSharkVideoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem* videoItem READ videoItem WRITE setVideoItem NOTIFY videoItemChanged)
    Q_PROPERTY(QString receiverName READ receiverName WRITE setReceiverName NOTIFY receiverNameChanged)
    Q_PROPERTY(QString uri READ uri WRITE setUri NOTIFY uriChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool lowLatency READ lowLatency WRITE setLowLatency NOTIFY lowLatencyChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(bool decoding READ decoding NOTIFY decodingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int startAttempts READ startAttempts NOTIFY startAttemptsChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)
    Q_PROPERTY(QString resolutionText READ resolutionText NOTIFY videoSizeChanged)
    Q_PROPERTY(QString frameRateText READ frameRateText NOTIFY frameRateTextChanged)
    Q_PROPERTY(quint64 frameCount READ frameCount NOTIFY frameCountChanged)

public:
    explicit DeepSharkVideoController(QObject *parent = nullptr);
    ~DeepSharkVideoController();

    QQuickItem *videoItem() const { return _videoItem; }
    QString receiverName() const { return _receiverName; }
    QString uri() const { return _uri; }
    bool autoStart() const { return _autoStart; }
    bool lowLatency() const { return _lowLatency; }
    bool streaming() const { return _streaming; }
    bool decoding() const { return _decoding; }
    QString statusText() const { return _statusText; }
    int startAttempts() const { return _startAttempts; }
    int videoWidth() const { return _videoSize.width(); }
    int videoHeight() const { return _videoSize.height(); }
    QString resolutionText() const;
    QString frameRateText() const;
    quint64 frameCount() const;

    void setVideoItem(QQuickItem *videoItem);
    void setReceiverName(const QString &receiverName);
    void setUri(const QString &uri);
    void setAutoStart(bool autoStart);
    void setLowLatency(bool lowLatency);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void videoItemChanged();
    void receiverNameChanged();
    void uriChanged();
    void autoStartChanged();
    void lowLatencyChanged();
    void streamingChanged();
    void decodingChanged();
    void statusTextChanged();
    void startAttemptsChanged();
    void videoSizeChanged();
    void frameRateTextChanged();
    void frameCountChanged();

private:
    void _ensureReceiver();
    void _rebuildSink();
    void _setStatusText(const QString &statusText);
    void _setStreaming(bool streaming);
    void _setDecoding(bool decoding);
    void _setVideoSize(const QSize &videoSize);
    void _setFrameRate(double frameRate);
    void _updateFrameRate();
    void _installSinkFrameProbe();
    void _removeSinkFrameProbe();

#ifdef QGC_GST_STREAMING
    static GstPadProbeReturn _videoSinkFrameProbe(GstPad *pad, GstPadProbeInfo *info, gpointer userData);
#endif

private:
    QPointer<QQuickItem> _videoItem;
    VideoReceiver *_receiver = nullptr;
    void *_sink = nullptr;
    QString _receiverName = QStringLiteral("deepSharkVideo");
    QString _uri;
    QString _statusText;
    bool _autoStart = false;
    bool _lowLatency = true;
    bool _streaming = false;
    bool _decoding = false;
    int _startAttempts = 0;
    QSize _videoSize;
    double _frameRate = 0.0;
    QTimer _frameRateTimer;

#ifdef QGC_GST_STREAMING
    gulong _sinkFrameProbeId = 0;
    std::atomic<quint64> _sinkFrameCount { 0 };
    quint64 _lastSinkFrameCount = 0;
#endif
};
