#include "DeepSharkVideoResizeTest.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QLoggingCategory>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoSink>
#include <QtNetwork/QUdpSocket>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <algorithm>
#include <array>
#include <memory>

#include "DeepSharkVideoController.h"
#include "Fact.h"
#include "SettingsManager.h"
#include "VideoBackend.h"
#include "VideoReceiver.h"
#include "VideoSettings.h"

#if defined(Q_OS_WIN) && defined(QGC_GST_STREAMING)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef QGC_GST_STREAMING
#include "GStreamer.h"
#include "MockVideoStreamServer.h"
#include "QGCQVideoSinkController.h"
#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
#include <gst/d3d11/gstd3d11.h>

#include "GstD3D11ContextBridge.h"
#include "QGCRhiCapture.h"
#endif
#endif

#if defined(Q_OS_WIN) && defined(QGC_GST_STREAMING)
namespace {

class NativeModalResizeSession : public QAbstractNativeEventFilter
{
public:
    NativeModalResizeSession(HWND window, int durationMs, int& resizeCount)
        : _window(window), _durationMs(durationMs), _resizeCount(resizeCount)
    {}

    bool run()
    {
        DWORD processId = 0;
        const DWORD threadId = GetWindowThreadProcessId(_window, &processId);
        if (!IsWindow(_window) || processId != GetCurrentProcessId() || threadId != GetCurrentThreadId()) {
            error = QStringLiteral("Sizing fixture HWND must belong to this process and GUI thread");
            return false;
        }
        if (s_active) {
            error = QStringLiteral("Another native sizing fixture is already active");
            return false;
        }
        if (!GetWindowRect(_window, &_lastRect)) {
            error = QStringLiteral("GetWindowRect failed: %1").arg(GetLastError());
            return false;
        }
        _initialRect = _lastRect;
        QCoreApplication::instance()->installNativeEventFilter(this);
        s_active = this;
        const auto cleanup = qScopeGuard([&]() {
            if (_timer)
                KillTimer(_window, _timer);
            _timer = 0;
            s_active = nullptr;
            QCoreApplication::instance()->removeNativeEventFilter(this);
        });
        _clock.start();
        _timer = SetTimer(_window, UINT_PTR(0x51474352), 1, &timerCallback);
        if (!_timer) {
            error = QStringLiteral("SetTimer failed: %1").arg(GetLastError());
            return false;
        }
        // Exercise resizing while DefWindowProc owns its modal message pump. The timer
        // changes only this HWND's size; this does not simulate physical mouse dragging.
        SendMessageW(_window, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOMRIGHT,
                     MAKELPARAM(_initialRect.right - 1, _initialRect.bottom - 1));
        return true;
    }

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* const nativeMessage = static_cast<MSG*>(message);
        if (nativeMessage->hwnd != _window)
            return false;
        switch (nativeMessage->message) {
            case WM_ENTERSIZEMOVE:
                ++entries;
                _insideSizing = true;
                break;
            case WM_SIZE:
                if (_insideSizing) {
                    ++resizeEvents;
                    ++_resizeCount;
                }
                break;
            case WM_EXITSIZEMOVE:
                ++exits;
                _insideSizing = false;
                break;
            default:
                break;
        }
        return false;
    }

    int entries = 0;
    int exits = 0;
    int resizeEvents = 0;
    int geometryChanges = 0;
    int timerTicks = 0;
    int failedPosts = 0;
    int failedResizeCalls = 0;
    QString error;

private:
    static void CALLBACK timerCallback(HWND window, UINT, UINT_PTR timer, DWORD)
    {
        if (s_active && s_active->_window == window && s_active->_timer == timer)
            s_active->tick();
    }

    void postKey(UINT key)
    {
        const LPARAM scanCode = LPARAM(MapVirtualKeyW(key, MAPVK_VK_TO_VSC)) << 16;
        const LPARAM keyDown = 1 | scanCode;
        if (!PostMessageW(_window, WM_KEYDOWN, key, keyDown))
            ++failedPosts;
        if (!PostMessageW(_window, WM_KEYUP, key, keyDown | (LPARAM(1) << 30) | (LPARAM(1) << 31)))
            ++failedPosts;
    }

    void tick()
    {
        ++timerTicks;
        RECT current{};
        if (GetWindowRect(_window, &current) && !EqualRect(&current, &_lastRect)) {
            ++geometryChanges;
            _lastRect = current;
        }
        const qint64 elapsed = _clock.elapsed();
        if (elapsed >= _durationMs) {
            postKey(elapsed < _durationMs + 1000 ? VK_RETURN : VK_ESCAPE);
            return;
        }
        if (!_insideSizing)
            return;
        const bool compactSize = (timerTicks % 2) != 0;
        const int width = _initialRect.right - _initialRect.left;
        const int height = _initialRect.bottom - _initialRect.top;
        if (!SetWindowPos(_window, nullptr, 0, 0, compactSize ? width * 3 / 5 : width,
                          compactSize ? height * 3 / 5 : height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
            ++failedResizeCalls;
    }

    static NativeModalResizeSession* s_active;
    HWND _window = nullptr;
    UINT_PTR _timer = 0;
    int _durationMs = 0;
    int& _resizeCount;
    bool _insideSizing = false;
    RECT _lastRect{};
    RECT _initialRect{};
    QElapsedTimer _clock;
};

NativeModalResizeSession* NativeModalResizeSession::s_active = nullptr;

}  // namespace
#endif

void DeepSharkVideoResizeTest::_threeLocalStreamsRemainResponsiveDuringResize()
{
#if !defined(QGC_GST_STREAMING) || !defined(Q_OS_WIN)
    QSKIP("Native Windows GStreamer resize stress requires a streaming-enabled Windows build");
#else
    if (QGuiApplication::platformName() != QStringLiteral("windows")) {
        QSKIP("Run with QT_QPA_PLATFORM=windows and --onscreen; offscreen does not exercise the native swapchain");
    }
    QLoggingCategory diagnostics("Test.DeepShark.VideoResize");
    diagnostics.setEnabled(QtInfoMsg, true);
    ignoreLogMessage(
        "Test.DeepShark.VideoResize", QtInfoMsg,
        QRegularExpression(QStringLiteral("^(Video resize |Native modal coverage:|Stream [0-2] decoded )")));
    const auto environment = GStreamer::prepareEnvironment();
    QVERIFY2(environment.ok, qPrintable(environment.error));
    QVERIFY(GStreamer::initialize({}, environment));
    if (!MockVideoStreamServer::isStreamTypeAvailable(MockVideoStreamServer::StreamType::RtpUdpH264)) {
        QSKIP("Local synthetic streams require videotestsrc and x264enc");
    }
    ignoreLogMessage("qt.qml.propertyCache.append", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Member enabled of the object QQuickPinchArea overrides")));
    startUI();
    if (QTest::currentTestFailed())
        return;
    if (!QSGRendererInterface::isApiRhiBased(_window->rendererInterface()->graphicsApi())) {
        QSKIP("Use a native RHI backend (for example QSG_RHI_BACKEND=d3d11), not QT_QUICK_BACKEND=software");
    }
    // QmlUITestBase boots MainWindow without VideoManager::init. Wire the backend's
    // normal render-device lifecycle without constructing its additional receivers.
    VideoBackend::onMainWindowReady(_window);
#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
    if (_window->rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11) {
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(QGCRhiCapture::deviceSnapshot().d3d11Device.load(std::memory_order_acquire) != nullptr,
                                 5000);
        qCInfo(diagnostics) << "Video resize startup: D3D11 render-device snapshot ready";
    }
#endif
    auto* const videoSettings = SettingsManager::instance()->videoSettings();
    QVERIFY(videoSettings);
    VideoBackend::applyDecoderPriorities(videoSettings->forceVideoDecoder()->rawValue().toInt());

    constexpr int streamCount = 3;
    std::array<QUdpSocket, streamCount> reservations;
    std::array<quint16, streamCount> ports{};
    for (int stream = 0; stream < streamCount; ++stream) {
        QVERIFY(reservations[stream].bind(QHostAddress::LocalHost, quint16(0)));
        ports[stream] = reservations[stream].localPort();
    }
    for (auto& reservation : reservations)
        reservation.close();
    std::array<MockVideoStreamServer, streamCount> servers;

    auto overlayObject = std::make_unique<QQuickItem>();
    auto* const overlay = overlayObject.get();
    overlay->setParentItem(_rootItem);
    overlay->setZ(1000);
    overlay->setSize(_rootItem->size());
    QQmlComponent tileComponent(_engine,
                                QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/VideoTile.qml")),
                                QQmlComponent::PreferSynchronous);
    QVERIFY2(tileComponent.isReady(), qPrintable(tileComponent.errorString()));
    std::array<std::unique_ptr<QObject>, streamCount> tiles;
    std::array<DeepSharkVideoController*, streamCount> controllers{};
    std::array<QVideoSink*, streamCount> sinks{};
    std::array<quint64, streamCount> displayed{};
    std::array<qint64, streamCount> lastDisplayMs{};
    std::array<qint64, streamCount> maxDisplayGapMs{};
    QElapsedTimer clock;
    clock.start();
    QObject progressOwner;
    auto arrange = [&]() {
        overlay->setSize(_rootItem->size());
        for (int stream = 0; stream < streamCount; ++stream) {
            auto* const tile = qobject_cast<QQuickItem*>(tiles[stream].get());
            if (!tile)
                continue;
            tile->setPosition(QPointF(stream * overlay->width() / streamCount, 0));
            tile->setSize(QSizeF(overlay->width() / streamCount, overlay->height()));
        }
    };
    QObject::connect(_rootItem, &QQuickItem::widthChanged, &progressOwner, arrange);
    QObject::connect(_rootItem, &QQuickItem::heightChanged, &progressOwner, arrange);
#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
    GstD3D11Device* sharedDevice = nullptr;
    const auto releaseSharedDevice = qScopeGuard([&]() { gst_clear_object(&sharedDevice); });
#endif
    const auto stopStreams = qScopeGuard([&]() {
        for (auto& tile : tiles) {
            if (tile)
                tile->setProperty("videoEnabled", false);
        }
    });
    for (int stream = 0; stream < streamCount; ++stream) {
        const QString streamUri = QStringLiteral("udp://127.0.0.1:%1").arg(ports[stream]);
        tiles[stream].reset(tileComponent.createWithInitialProperties({
            {QStringLiteral("parent"), QVariant::fromValue(overlay)},
            {QStringLiteral("videoEnabled"), true},
            {QStringLiteral("videoSource"), streamUri},
            {QStringLiteral("receiverName"), QStringLiteral("resizeStress%1").arg(stream)},
            {QStringLiteral("title"), QStringLiteral("Local synthetic %1").arg(stream)},
            {QStringLiteral("dragEnabled"), false},
        }));
        auto* const tile = qobject_cast<QQuickItem*>(tiles[stream].get());
        QVERIFY2(tile, qPrintable(tileComponent.errorString()));
        QCOMPARE(tile->window(), _window);
        controllers[stream] = tile->findChild<DeepSharkVideoController*>();
        QVERIFY(controllers[stream]);
        auto* const output = qvariant_cast<QObject*>(tile->property("previewItem"));
        QVERIFY(output);
        QCOMPARE(qobject_cast<QQuickItem*>(output)->window(), _window);
        sinks[stream] = qvariant_cast<QVideoSink*>(output->property("videoSink"));
        QVERIFY(sinks[stream]);
        QObject::connect(
            sinks[stream], &QVideoSink::videoFrameChanged, &progressOwner, [&, stream](const QVideoFrame& frame) {
                if (!frame.isValid())
                    return;
                ++displayed[stream];
                const qint64 now = clock.elapsed();
                if (lastDisplayMs[stream] > 0)
                    maxDisplayGapMs[stream] = std::max(maxDisplayGapMs[stream], now - lastDisplayMs[stream]);
                lastDisplayMs[stream] = now;
            });
        arrange();
        auto* const receiver = controllers[stream]->findChild<VideoReceiver*>();
        QVERIFY(receiver);
        QTRY_VERIFY_WITH_TIMEOUT(receiver->started(), 5000);
        // Listen before the sender starts, preserving the initial SPS/PPS/IDR on the
        // low-latency RTP path instead of making startup depend on late-join recovery.
        QVERIFY(servers[stream].start(MockVideoStreamServer::StreamType::RtpUdpH264, QStringLiteral("127.0.0.1"),
                                      ports[stream]));
        QCOMPARE(servers[stream].servedUri(), streamUri);
        const auto startupProgress = qScopeGuard([&, stream]() {
            qCInfo(diagnostics) << "Video resize stream startup:" << stream << "streaming"
                                << controllers[stream]->streaming() << "decoding" << controllers[stream]->decoding()
                                << "displayed" << displayed[stream] << "status" << controllers[stream]->statusText();
            if (QTest::currentTestFailed()) {
                const auto* currentReceiver = controllers[stream]->findChild<VideoReceiver*>();
                for (const auto* sinkController : QGCQVideoSinkController::controllersOf(currentReceiver)) {
                    gboolean active = FALSE;
                    guint64 input = 0, dropped = 0, delivered = 0;
                    gpointer target = nullptr;
                    g_object_get(const_cast<GstElement*>(sinkController->element()), "active", &active, "frames-input",
                                 &input, "frames-dropped", &dropped, "frames-delivered", &delivered, "qvideosink",
                                 &target, nullptr);
                    qCInfo(diagnostics) << "Video resize startup sink:" << stream << "active" << active << "input"
                                        << input << "dropped" << dropped << "delivered" << delivered
                                        << "expected target" << (target == sinks[stream]) << "window visibility"
                                        << _window->visibility();
                }
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(controllers[stream]->decoding() && displayed[stream] >= 10, 15000);
        QCOMPARE(sinks[stream]->videoFrame().size(), QSize(1280, 720));
#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
        if (_window->rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11) {
            auto* const device = GstD3D11ContextBridge::currentDevice();
            QVERIFY2(device, "D3D11 stream must prime the shared decoder/render device");
            if (!sharedDevice) {
                sharedDevice = device;
            } else {
                const bool unchanged = device == sharedDevice;
                gst_object_unref(device);
                QVERIFY2(unchanged, "Starting another video receiver replaced the live shared D3D11 wrapper");
            }
        }
#endif
    }

    const QRect available = _window->screen()->availableGeometry().adjusted(24, 48, -24, -48);
    _window->setGeometry(available);
    const int minWidth = std::max(_window->minimumWidth(), available.width() * 3 / 5);
    const int minHeight = std::max(_window->minimumHeight(), available.height() * 3 / 5);
    const int durationMs = std::clamp(qEnvironmentVariableIntValue("QGC_TEST_VIDEO_RESIZE_MS"), 15000, 300000);
    const bool nativeModal = qEnvironmentVariableIntValue("QGC_TEST_VIDEO_NATIVE_MODAL") == 1;
    std::array<quint64, streamCount> initialDecoded{};
    std::array<quint64, streamCount> initialDisplayed = displayed;
    for (int stream = 0; stream < streamCount; ++stream) {
        initialDecoded[stream] = controllers[stream]->totalFrameCount();
        lastDisplayMs[stream] = clock.elapsed();
        maxDisplayGapMs[stream] = 0;
    }
    int resizeCount = 0;
    int actualResizeCount = 0;
    QRect lastGeometry = _window->geometry();
    QSize minimumObservedSize = lastGeometry.size();
    QSize maximumObservedSize = lastGeometry.size();
    const auto observeGeometry = [&]() {
        const QRect geometry = _window->geometry();
        if (geometry != lastGeometry) {
            ++actualResizeCount;
            lastGeometry = geometry;
            minimumObservedSize.setWidth(std::min(minimumObservedSize.width(), geometry.width()));
            minimumObservedSize.setHeight(std::min(minimumObservedSize.height(), geometry.height()));
            maximumObservedSize.setWidth(std::max(maximumObservedSize.width(), geometry.width()));
            maximumObservedSize.setHeight(std::max(maximumObservedSize.height(), geometry.height()));
        }
    };
    QObject::connect(_window, &QWindow::widthChanged, &progressOwner, observeGeometry);
    QObject::connect(_window, &QWindow::heightChanged, &progressOwner, observeGeometry);
    int guiTicks = 0;
    quint64 renderCount = 0;
    qint64 lastGuiMs = clock.elapsed();
    qint64 maxGuiGapMs = 0;
    QObject::connect(_window, &QQuickWindow::frameSwapped, &progressOwner, [&]() { ++renderCount; });
    QTimer resizeTimer;
    resizeTimer.setTimerType(Qt::PreciseTimer);
    resizeTimer.setInterval(1);
    QObject::connect(&resizeTimer, &QTimer::timeout, this, [&]() {
        const bool compactSize = (resizeCount++ % 2) != 0;
        _window->setGeometry(available.x(), available.y(), compactSize ? minWidth : available.width(),
                             compactSize ? minHeight : available.height());
    });
    QTimer guiTimer;
    guiTimer.setTimerType(Qt::PreciseTimer);
    guiTimer.setInterval(25);
    QObject::connect(&guiTimer, &QTimer::timeout, this, [&]() {
        ++guiTicks;
        const qint64 now = clock.elapsed();
        maxGuiGapMs = std::max(maxGuiGapMs, now - lastGuiMs);
        lastGuiMs = now;
    });
    QTimer progressTimer;
    progressTimer.setInterval(1000);
    QObject::connect(&progressTimer, &QTimer::timeout, this, [&]() {
        qCInfo(diagnostics) << "Video resize progress:" << clock.elapsed() << "ms; resize requests" << resizeCount
                            << "actual geometry changes" << actualResizeCount << "GUI ticks" << guiTicks << "rendered"
                            << renderCount << "displayed" << displayed[0] << displayed[1] << displayed[2]
                            << "max GUI gap" << maxGuiGapMs;
    });
    QEventLoop stressLoop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &stressLoop, &QEventLoop::quit);
    qCInfo(diagnostics) << "Video resize fixture: 3 x local RTP/H.264 1280x720 @ 30 FPS; RHI"
                        << _window->rendererInterface()->graphicsApi() << "QSG_RENDER_LOOP"
                        << qEnvironmentVariable("QSG_RENDER_LOOP", QStringLiteral("default")) << "duration"
                        << durationMs << "Windows modal with programmatic resize" << nativeModal
                        << "large/small alternation on each callback; requested interval 1 ms";
    QElapsedTimer resizeClock;
    resizeClock.start();
    guiTimer.start();
    progressTimer.start();
    if (nativeModal) {
        NativeModalResizeSession session(reinterpret_cast<HWND>(_window->winId()), durationMs, resizeCount);
        QVERIFY2(session.run(), qPrintable(session.error));
        qCInfo(diagnostics) << "Native modal coverage: entries" << session.entries << "exits" << session.exits
                            << "WM_SIZE" << session.resizeEvents << "geometry changes" << session.geometryChanges
                            << "timer callbacks" << session.timerTicks << "failed posts" << session.failedPosts
                            << "failed resize calls" << session.failedResizeCalls;
        QCOMPARE(session.entries, 1);
        QCOMPARE(session.exits, session.entries);
        QVERIFY2(session.resizeEvents > 0, "Fixture did not receive WM_SIZE during the native modal loop");
        QVERIFY2(session.geometryChanges > 0, "Fixture did not change window geometry during the native modal loop");
        QCOMPARE(session.failedPosts, 0);
        QCOMPARE(session.failedResizeCalls, 0);
    } else {
        resizeTimer.start();
        deadline.start(durationMs);
        stressLoop.exec();
        resizeTimer.stop();
    }
    guiTimer.stop();
    progressTimer.stop();
    const qint64 elapsedResizeMs = resizeClock.elapsed();
    qCInfo(diagnostics) << "Video resize completed:" << elapsedResizeMs << "ms; actual geometry changes"
                        << actualResizeCount << "changes/sec" << actualResizeCount * 1000.0 / elapsedResizeMs
                        << "size range" << minimumObservedSize << "to" << maximumObservedSize << "GUI ticks" << guiTicks
                        << "rendered" << renderCount << "max GUI gap" << maxGuiGapMs;

    QVERIFY2(elapsedResizeMs >= durationMs, "Resize stress ended before the requested duration");
    QVERIFY2(resizeCount >= durationMs / 100, "Resize callbacks stopped progressing");
    QVERIFY2(actualResizeCount >= durationMs / 100, "Main-window geometry stopped changing");
    QVERIFY2(guiTicks >= durationMs / 100, "GUI heartbeat stopped progressing");
    QVERIFY2(renderCount >= quint64(durationMs / 200), "Native render loop stopped progressing");
    QVERIFY2(maxGuiGapMs < 2000, "GUI was unresponsive for at least two seconds");
    for (int stream = 0; stream < streamCount; ++stream) {
        qCInfo(diagnostics) << "Stream" << stream << "decoded"
                            << controllers[stream]->totalFrameCount() - initialDecoded[stream] << "displayed"
                            << displayed[stream] - initialDisplayed[stream] << "max display gap"
                            << maxDisplayGapMs[stream];
        QVERIFY(controllers[stream]->decoding());
        QVERIFY(controllers[stream]->totalFrameCount() > initialDecoded[stream] + quint64(durationMs / 200));
        QVERIFY(displayed[stream] > initialDisplayed[stream] + quint64(durationMs / 200));
        QVERIFY2(maxDisplayGapMs[stream] < 3000, "A video sink stopped receiving frames during resize");
        QVERIFY2(clock.elapsed() - lastDisplayMs[stream] < 1000, "A video sink did not recover at the end of resize");
    }
    const auto completedFrames = displayed;
    _window->setGeometry(available);
    for (int stream = 0; stream < streamCount; ++stream) {
        QTRY_VERIFY_WITH_TIMEOUT(displayed[stream] >= completedFrames[stream] + 10, 3000);
    }
#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
    if (sharedDevice) {
        auto* const device = GstD3D11ContextBridge::currentDevice();
        const bool unchanged = device == sharedDevice;
        if (device)
            gst_object_unref(device);
        QVERIFY2(unchanged, "Window resizing replaced the shared D3D11 wrapper while streams were live");
    }
#endif
    const QImage screenshot = _window->grabWindow();
    QVERIFY(!screenshot.isNull());
    const QString preview = QStringLiteral("video-resize-%1%2.png")
                                .arg(qEnvironmentVariable("QSG_RENDER_LOOP", QStringLiteral("default")),
                                     nativeModal ? QStringLiteral("-native-modal") : QString());
    QVERIFY(screenshot.save(QDir::current().filePath(preview)));
#endif
}

UT_REGISTER_TEST(DeepSharkVideoResizeTest, TestLabel::Integration)
