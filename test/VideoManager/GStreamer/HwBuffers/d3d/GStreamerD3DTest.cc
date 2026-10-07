#include "GStreamerTest.h"

#ifdef QGC_GST_STREAMING

#include <QtCore/QScopeGuard>
#include <QtMultimedia/QVideoFrameFormat>
#include <algorithm>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video-info.h>
#include <memory>
#include <vector>

#include "GStreamer.h"
#include "GStreamerLogging.h"
#include "GstHwVideoBuffer.h"
#include "GstHwVideoBufferFactory.h"

#if defined(Q_OS_WIN) && (defined(QGC_HAS_GST_D3D11_GPU_PATH) || defined(QGC_HAS_GST_D3D12_GPU_PATH))
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include "QGCRhiCapture.h"
#include "GstHwFrameTexturesBase.h"

#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
#include <d3d11.h>
#include <gst/d3d11/gstd3d11.h>

#include "GstD3D11ContextBridge.h"
#endif
#if defined(QGC_HAS_GST_D3D12_GPU_PATH)
#include "GstD3D12ContextBridge.h"
#endif

namespace {

qint64 composeLuid(qint32 high, quint32 low)
{
    return (static_cast<qint64>(high) << 32) | (static_cast<qint64>(low) & 0xFFFFFFFFLL);
}

struct SnapshotGuard
{
    SnapshotGuard()
        : backend(QGCRhiCapture::deviceSnapshot().backend.load(std::memory_order_acquire)),
          d3d11Device(QGCRhiCapture::deviceSnapshot().d3d11Device.load(std::memory_order_acquire)),
          d3d12Device(QGCRhiCapture::deviceSnapshot().d3d12Device.load(std::memory_order_acquire)),
          adapterLuid(QGCRhiCapture::deviceSnapshot().adapterLuid.load(std::memory_order_acquire))
    {}

    ~SnapshotGuard()
    {
        QGCRhiCapture::deviceSnapshot().d3d11Device.store(d3d11Device, std::memory_order_release);
        QGCRhiCapture::deviceSnapshot().d3d12Device.store(d3d12Device, std::memory_order_release);
        QGCRhiCapture::deviceSnapshot().adapterLuid.store(adapterLuid, std::memory_order_release);
        QGCRhiCapture::deviceSnapshot().backend.store(backend, std::memory_order_release);
    }

    int backend = -1;
    void* d3d11Device = nullptr;
    void* d3d12Device = nullptr;
    qint64 adapterLuid = 0;
};

struct D3DSample
{
    GstElement* pipeline = nullptr;
    GstElement* sink = nullptr;
    GstSample* sample = nullptr;
    GstVideoInfo info;

    ~D3DSample()
    {
        if (sample) {
            gst_sample_unref(sample);
        }
        if (sink) {
            gst_object_unref(sink);
        }
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
    }
};

void pullD3DSample(D3DSample& result, const char* inputCaps, const char* uploadElement, const char* capsFilter,
                   HwVideoBufferPath bridgePath = HwVideoBufferPath::None)
{
    GstElementFactory* uploadFactory = gst_element_factory_find(uploadElement);
    if (!uploadFactory) {
        QSKIP(qPrintable(QStringLiteral("%1 factory unavailable").arg(QString::fromUtf8(uploadElement))));
    }
    gst_object_unref(uploadFactory);

    const QString launch = QStringLiteral(
                               "videotestsrc num-buffers=1 ! "
                               "%1 ! "
                               "%2 ! %3 ! appsink name=sink sync=false")
                               .arg(QString::fromUtf8(inputCaps))
                               .arg(QString::fromUtf8(uploadElement))
                               .arg(QString::fromUtf8(capsFilter));

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(launch.toUtf8().constData(), &error);
    if (error) {
        const QString msg = QString::fromUtf8(error->message);
        g_clear_error(&error);
        QSKIP(qPrintable(QStringLiteral("D3D pipeline parse skipped: %1").arg(msg)));
    }
    QVERIFY2(pipeline, "Failed to create D3D dispatch test pipeline");

    result.pipeline = pipeline;

    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    QVERIFY2(sink, "Could not find appsink");
    result.sink = sink;

    if (bridgePath != HwVideoBufferPath::None) {
        GstBus* bus = gst_element_get_bus(pipeline);
        QVERIFY2(bus, "Could not get D3D test pipeline bus");
        gst_bus_set_sync_handler(
            bus,
            [](GstBus*, GstMessage* message, gpointer userData) -> GstBusSyncReply {
                const HwVideoBufferPath path = static_cast<HwVideoBufferPath>(GPOINTER_TO_INT(userData));
                switch (path) {
#if defined(QGC_HAS_GST_D3D11_GPU_PATH)
                    case HwVideoBufferPath::D3D11:
                        return GstD3D11ContextBridge::handleSyncMessage(message);
#endif
#if defined(QGC_HAS_GST_D3D12_GPU_PATH)
                    case HwVideoBufferPath::D3D12:
                        return GstD3D12ContextBridge::handleSyncMessage(message);
#endif
                    default:
                        break;
                }
                return GST_BUS_PASS;
            },
            GINT_TO_POINTER(static_cast<int>(bridgePath)), nullptr);
        gst_object_unref(bus);
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        QSKIP("D3D pipeline failed to PLAY");
    }

    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
    if (!sample) {
        QSKIP("D3D pipeline produced no sample");
    }
    result.sample = sample;

    gst_video_info_init(&result.info);
    QVERIFY2(gst_video_info_from_caps(&result.info, gst_sample_get_caps(sample)), "Could not parse D3D sample caps");
}

void testD3DMemoryDispatch(const char* uploadElement, const char* capsFilter, HwVideoBufferPath expectedPath)
{
    GStreamer::redirectGLibLogging();
    QVERIFY2(GStreamer::completeInit(), "GStreamer::completeInit() failed");

    D3DSample sample;
    pullD3DSample(sample, "video/x-raw,format=RGBA,width=64,height=64,framerate=30/1", uploadElement, capsFilter);

    HwVideoBufferContext context;
    context.gpuEnabled = true;
    HwResolvedPathCache cache;
    HwVideoBufferPath path = HwVideoBufferPath::None;
    QVideoFrameFormat format(QSize(GST_VIDEO_INFO_WIDTH(&sample.info), GST_VIDEO_INFO_HEIGHT(&sample.info)),
                             QVideoFrameFormat::Format_RGBA8888);

    auto buffer = makeHwVideoBuffer(sample.sample, sample.info, format, context, path, &cache);

    QVERIFY2(buffer, "D3D memory sample did not create a hardware video buffer");
    QCOMPARE(path, expectedPath);
    QVERIFY(cache.validated);
    QCOMPARE(cache.path, expectedPath);
}

void testD3DMapTextures(QRhi& rhi, const char* inputCaps, const char* uploadElement, const char* capsFilter,
                        QVideoFrameFormat::PixelFormat pixelFormat, int expectedPlanes, HwVideoBufferPath expectedPath)
{
    GStreamer::redirectGLibLogging();
    QVERIFY2(GStreamer::completeInit(), "GStreamer::completeInit() failed");

    D3DSample sample;
    pullD3DSample(sample, inputCaps, uploadElement, capsFilter, expectedPath);

    HwVideoBufferContext context;
    context.gpuEnabled = true;
    HwResolvedPathCache cache;
    HwVideoBufferPath path = HwVideoBufferPath::None;
    QVideoFrameFormat format(QSize(GST_VIDEO_INFO_WIDTH(&sample.info), GST_VIDEO_INFO_HEIGHT(&sample.info)),
                             pixelFormat);

    auto buffer = makeHwVideoBuffer(sample.sample, sample.info, format, context, path, &cache);

    QVERIFY2(buffer, "D3D memory sample did not create a hardware video buffer");
    QCOMPARE(path, expectedPath);

    QVideoFrameTexturesUPtr oldTextures;
    QVideoFrameTexturesUPtr textures = buffer->mapTextures(rhi, oldTextures);
    QVERIFY2(textures, "D3D hardware video buffer did not import into the active QRhi");
    for (int plane = 0; plane < expectedPlanes; ++plane) {
        QVERIFY2(textures->texture(static_cast<uint>(plane)),
                 qPrintable(QStringLiteral("D3D QRhi texture bundle has no plane %1 texture").arg(plane)));
    }
    auto* base = dynamic_cast<GstHwFrameTexturesBase*>(textures.get());
    QVERIFY2(base, "D3D texture bundle does not use the HW frame texture base");
    QCOMPARE(base->sourcePath(), expectedPath);
}

} // namespace
#endif

void GStreamerTest::_testD3D11MemoryDispatch()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D11_GPU_PATH)
    testD3DMemoryDispatch("d3d11upload", "video/x-raw(memory:D3D11Memory)", HwVideoBufferPath::D3D11);
#else
    QSKIP("D3D11 GPU path not compiled in this build");
#endif
}

void GStreamerTest::_testD3D12MemoryDispatch()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D12_GPU_PATH)
    testD3DMemoryDispatch("d3d12upload", "video/x-raw(memory:D3D12Memory)", HwVideoBufferPath::D3D12);
#else
    QSKIP("D3D12 GPU path not compiled in this build");
#endif
}

void GStreamerTest::_testD3D11MapTexturesWithQRhi()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D11_GPU_PATH)
    QRhiD3D11InitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D11, &params));
    if (!rhi) {
        QSKIP("Could not create D3D11 QRhi");
    }
    auto* handles = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
    if (!handles || !handles->dev) {
        QSKIP("D3D11 QRhi exposes no native device handle");
    }

    SnapshotGuard snapshotGuard;
    Q_UNUSED(snapshotGuard)
    QGCRhiCapture::deviceSnapshot().d3d11Device.store(handles->dev, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().d3d12Device.store(nullptr, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().adapterLuid.store(composeLuid(handles->adapterLuidHigh, handles->adapterLuidLow),
                                                      std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().backend.store(static_cast<int>(QRhi::D3D11), std::memory_order_release);

    GstD3D11ContextBridge::reset();
    auto bridgeGuard = qScopeGuard([] { GstD3D11ContextBridge::reset(); });
    QVERIFY2(GstD3D11ContextBridge::prime(), "Could not prime D3D11 context bridge from QRhi snapshot");

    testD3DMapTextures(*rhi, "video/x-raw,format=RGBA,width=64,height=64,framerate=30/1", "d3d11upload",
                       "video/x-raw(memory:D3D11Memory)", QVideoFrameFormat::Format_RGBA8888, 1,
                       HwVideoBufferPath::D3D11);
#else
    QSKIP("D3D11 GPU path not compiled in this build");
#endif
}

void GStreamerTest::_testD3D11MapNv12TexturesWithQRhi()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D11_GPU_PATH)
    QRhiD3D11InitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D11, &params));
    if (!rhi) {
        QSKIP("Could not create D3D11 QRhi");
    }
    auto* handles = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
    if (!handles || !handles->dev) {
        QSKIP("D3D11 QRhi exposes no native device handle");
    }

    SnapshotGuard snapshotGuard;
    Q_UNUSED(snapshotGuard)
    QGCRhiCapture::deviceSnapshot().d3d11Device.store(handles->dev, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().d3d12Device.store(nullptr, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().adapterLuid.store(composeLuid(handles->adapterLuidHigh, handles->adapterLuidLow),
                                                      std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().backend.store(static_cast<int>(QRhi::D3D11), std::memory_order_release);

    GstD3D11ContextBridge::reset();
    auto bridgeGuard = qScopeGuard([] { GstD3D11ContextBridge::reset(); });
    QVERIFY2(GstD3D11ContextBridge::prime(), "Could not prime D3D11 context bridge from QRhi snapshot");

    testD3DMapTextures(*rhi, "video/x-raw,format=NV12,width=64,height=64,framerate=30/1", "d3d11upload",
                       "video/x-raw(memory:D3D11Memory),format=NV12", QVideoFrameFormat::Format_NV12, 2,
                       HwVideoBufferPath::D3D11);
#else
    QSKIP("D3D11 GPU path not compiled in this build");
#endif
}

void GStreamerTest::_testD3D11PaddedFrameViewport_data()
{
    QTest::addColumn<QSize>("visibleSize");
    QTest::addColumn<QRect>("viewport");
    QTest::addColumn<bool>("encodedSource");
    QTest::newRow("no-padding") << QSize(640, 368) << QRect(0, 0, 640, 368) << false;
    QTest::newRow("bottom-padding") << QSize(640, 360) << QRect(0, 0, 640, 360) << false;
    QTest::newRow("right-and-bottom-padding") << QSize(636, 360) << QRect(0, 0, 636, 360) << false;
    QTest::newRow("explicit-crop") << QSize(640, 360) << QRect(4, 8, 632, 344) << false;
    QTest::newRow("decoder-array-padding") << QSize(640, 360) << QRect(0, 0, 640, 360) << true;
}

void GStreamerTest::_testD3D11PaddedFrameViewport()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D11_GPU_PATH)
    QFETCH(QSize, visibleSize);
    QFETCH(QRect, viewport);
    QFETCH(bool, encodedSource);
    QRhiD3D11InitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D11, &params));
    if (!rhi) {
        QSKIP("Could not create D3D11 QRhi");
    }
    auto* handles = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
    QVERIFY2(handles && handles->dev, "D3D11 QRhi exposes no native device handle");

    SnapshotGuard snapshotGuard;
    Q_UNUSED(snapshotGuard)
    QGCRhiCapture::deviceSnapshot().d3d11Device.store(handles->dev, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().d3d12Device.store(nullptr, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().adapterLuid.store(composeLuid(handles->adapterLuidHigh, handles->adapterLuidLow),
                                                      std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().backend.store(static_cast<int>(QRhi::D3D11), std::memory_order_release);
    GstD3D11ContextBridge::reset();
    auto bridgeGuard = qScopeGuard([] { GstD3D11ContextBridge::reset(); });
    QVERIFY2(GstD3D11ContextBridge::prime(), "Could not prime D3D11 context bridge from QRhi snapshot");
    QVERIFY2(GStreamer::completeInit(), "GStreamer::completeInit() failed");

    D3DSample sample;
    if (encodedSource) {
        pullD3DSample(sample, "video/x-raw,format=I420,width=640,height=360,framerate=30/1 ! openh264enc ! h264parse",
                      "d3d11h264dec", "video/x-raw(memory:D3D11Memory),format=NV12", HwVideoBufferPath::D3D11);
    } else {
        // Model a decoder whose NV12 allocation includes rows beyond the negotiated picture.
        pullD3DSample(sample, "video/x-raw,format=NV12,width=640,height=368,framerate=30/1", "d3d11upload",
                      "video/x-raw(memory:D3D11Memory),format=NV12", HwVideoBufferPath::D3D11);
    }
    QVERIFY(sample.sample);
    GstVideoInfo visibleInfo;
    gst_video_info_init(&visibleInfo);
    QVERIFY(gst_video_info_set_format(&visibleInfo, GST_VIDEO_FORMAT_NV12, visibleSize.width(), visibleSize.height()));
    QVideoFrameFormat format(visibleSize, QVideoFrameFormat::Format_NV12);
    format.setViewport(viewport);
    HwVideoBufferContext context;
    context.gpuEnabled = true;
    HwVideoBufferPath path = HwVideoBufferPath::None;
    auto buffer = makeHwVideoBuffer(sample.sample, visibleInfo, format, context, path);
    QVERIFY2(buffer, "Padded D3D11 sample did not create a hardware video buffer");
    QCOMPARE(path, HwVideoBufferPath::D3D11);
    QCOMPARE(buffer->format().frameSize(), visibleSize);
    QCOMPARE(buffer->format().viewport(), viewport);

    QVideoFrameTexturesUPtr oldTextures;
    auto textures = buffer->mapTextures(*rhi, oldTextures);
    QVERIFY(textures);
    QVERIFY(textures->texture(0));
    QVERIFY(textures->texture(1));
    QCOMPARE(textures->texture(0)->pixelSize(), visibleSize);
    QCOMPARE(textures->texture(1)->pixelSize(), visibleSize / 2);
    auto* nativeTexture = reinterpret_cast<ID3D11Texture2D*>(textures->texture(0)->nativeTexture().object);
    QVERIFY(nativeTexture);
    D3D11_TEXTURE2D_DESC nativeDesc{};
    nativeTexture->GetDesc(&nativeDesc);
    QCOMPARE(nativeDesc.Width, static_cast<UINT>(visibleSize.width()));
    QCOMPARE(nativeDesc.Height, static_cast<UINT>(visibleSize.height()));
    QCOMPARE(nativeDesc.ArraySize, 1U);

    // Verify both NV12 planes were copied, rather than only changing the advertised dimensions.
    auto* device = static_cast<ID3D11Device*>(handles->dev);
    ID3D11DeviceContext* deviceContext = nullptr;
    device->GetImmediateContext(&deviceContext);
    QVERIFY(deviceContext);
    auto contextGuard = qScopeGuard([deviceContext] { deviceContext->Release(); });
    auto download = [&](ID3D11Texture2D* texture, UINT subresource, QByteArray& pixels) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.ArraySize = 1;
        desc.MipLevels = 1;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* readback = nullptr;
        QVERIFY(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback)));
        auto readbackGuard = qScopeGuard([readback] { readback->Release(); });
        deviceContext->CopySubresourceRegion(readback, 0, 0, 0, 0, texture, subresource, nullptr);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        QVERIFY(SUCCEEDED(deviceContext->Map(readback, 0, D3D11_MAP_READ, 0, &mapped)));
        auto mapGuard = qScopeGuard([&] { deviceContext->Unmap(readback, 0); });
        const int rows = static_cast<int>(desc.Height) * 3 / 2;
        for (int y = 0; y < rows; ++y) {
            pixels.append(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch, static_cast<int>(desc.Width));
        }
    };
    GstBuffer* sourceBuffer = gst_sample_get_buffer(sample.sample);
    auto* sourceMemory = GST_D3D11_MEMORY_CAST(gst_buffer_peek_memory(sourceBuffer, 0));
    auto* sourceTexture = reinterpret_cast<ID3D11Texture2D*>(gst_d3d11_memory_get_resource_handle(sourceMemory));
    QVERIFY(sourceTexture);
    D3D11_TEXTURE2D_DESC sourceDesc{};
    sourceTexture->GetDesc(&sourceDesc);
    QByteArray sourcePixels;
    QByteArray croppedPixels;
    download(sourceTexture, gst_d3d11_memory_get_subresource_index(sourceMemory), sourcePixels);
    download(nativeTexture, 0, croppedPixels);
    QVERIFY(!sourcePixels.isEmpty());
    QVERIFY(!croppedPixels.isEmpty());
    for (int y = 0; y < visibleSize.height() * 3 / 2; ++y) {
        const int sourceRow =
            y < visibleSize.height() ? y : y - visibleSize.height() + static_cast<int>(sourceDesc.Height);
        QCOMPARE(croppedPixels.mid(y * visibleSize.width(), visibleSize.width()),
                 sourcePixels.mid(sourceRow * static_cast<int>(sourceDesc.Width), visibleSize.width()));
    }
    if (sourceDesc.Width != nativeDesc.Width || sourceDesc.Height != nativeDesc.Height || sourceDesc.ArraySize > 1) {
        // Multiple streams may retain more frames than the former global three-slot staging ring.
        // None of those still-live frames may share a texture that a later copy overwrites.
        std::vector<QVideoFrameTexturesUPtr> heldTextures;
        std::vector<quint64> heldResources{textures->texture(0)->nativeTexture().object};
        for (int i = 0; i < 4; ++i) {
            auto nextBuffer = makeHwVideoBuffer(sample.sample, visibleInfo, format, context, path);
            QVERIFY(nextBuffer);
            QVideoFrameTexturesUPtr previous;
            auto nextTextures = nextBuffer->mapTextures(*rhi, previous);
            QVERIFY(nextTextures);
            QVERIFY(nextTextures->texture(0));
            const quint64 resource = nextTextures->texture(0)->nativeTexture().object;
            QVERIFY2(std::find(heldResources.begin(), heldResources.end(), resource) == heldResources.end(),
                     "A staging texture is still held by an earlier frame");
            heldResources.push_back(resource);
            heldTextures.push_back(std::move(nextTextures));
        }
    }
    QVideoFrame frame(std::move(buffer));
    QCOMPARE(frame.surfaceFormat().viewport(), viewport);
    QCOMPARE(frame.size(), visibleSize);
#else
    QSKIP("D3D11 GPU path not compiled in this build");
#endif
}

void GStreamerTest::_testD3D12MapTexturesWithQRhi()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D12_GPU_PATH)
    QRhiD3D12InitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D12, &params));
    if (!rhi) {
        QSKIP("Could not create D3D12 QRhi");
    }
    auto* handles = static_cast<const QRhiD3D12NativeHandles*>(rhi->nativeHandles());
    if (!handles || !handles->dev) {
        QSKIP("D3D12 QRhi exposes no native device handle");
    }

    SnapshotGuard snapshotGuard;
    Q_UNUSED(snapshotGuard)
    QGCRhiCapture::deviceSnapshot().d3d11Device.store(nullptr, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().d3d12Device.store(handles->dev, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().adapterLuid.store(composeLuid(handles->adapterLuidHigh, handles->adapterLuidLow),
                                                      std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().backend.store(static_cast<int>(QRhi::D3D12), std::memory_order_release);

    GstD3D12ContextBridge::reset();
    auto bridgeGuard = qScopeGuard([] { GstD3D12ContextBridge::reset(); });
    QVERIFY2(GstD3D12ContextBridge::prime(), "Could not prime D3D12 context bridge from QRhi snapshot");

    testD3DMapTextures(*rhi, "video/x-raw,format=RGBA,width=64,height=64,framerate=30/1", "d3d12upload",
                       "video/x-raw(memory:D3D12Memory)", QVideoFrameFormat::Format_RGBA8888, 1,
                       HwVideoBufferPath::D3D12);
#else
    QSKIP("D3D12 GPU path not compiled in this build");
#endif
}

void GStreamerTest::_testD3D12MapNv12TexturesWithQRhi()
{
#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D12_GPU_PATH)
    QRhiD3D12InitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D12, &params));
    if (!rhi) {
        QSKIP("Could not create D3D12 QRhi");
    }
    auto* handles = static_cast<const QRhiD3D12NativeHandles*>(rhi->nativeHandles());
    if (!handles || !handles->dev) {
        QSKIP("D3D12 QRhi exposes no native device handle");
    }

    SnapshotGuard snapshotGuard;
    Q_UNUSED(snapshotGuard)
    QGCRhiCapture::deviceSnapshot().d3d11Device.store(nullptr, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().d3d12Device.store(handles->dev, std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().adapterLuid.store(composeLuid(handles->adapterLuidHigh, handles->adapterLuidLow),
                                                      std::memory_order_release);
    QGCRhiCapture::deviceSnapshot().backend.store(static_cast<int>(QRhi::D3D12), std::memory_order_release);

    GstD3D12ContextBridge::reset();
    auto bridgeGuard = qScopeGuard([] { GstD3D12ContextBridge::reset(); });
    QVERIFY2(GstD3D12ContextBridge::prime(), "Could not prime D3D12 context bridge from QRhi snapshot");

    testD3DMapTextures(*rhi, "video/x-raw,format=NV12,width=64,height=64,framerate=30/1", "d3d12upload",
                       "video/x-raw(memory:D3D12Memory),format=NV12", QVideoFrameFormat::Format_NV12, 2,
                       HwVideoBufferPath::D3D12);
#else
    QSKIP("D3D12 GPU path not compiled in this build");
#endif
}

#endif
