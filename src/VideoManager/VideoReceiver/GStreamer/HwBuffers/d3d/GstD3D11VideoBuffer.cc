#include "GstD3D11VideoBuffer.h"

#if defined(Q_OS_WIN) && defined(QGC_HAS_GST_D3D11_GPU_PATH)

#include <algorithm>
#include <d3d11.h>
#include <gst/d3d11/gstd3d11.h>

#include "GstContextBridgeRegistry.h"
#include "GstD3D11ContextBridge.h"
#include "GstD3DContextBridgeCommon.h"
#include "GstD3DVideoBufferCommon.h"
#include "GstHwPathTelemetry.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(GstD3D11Log, "Video.GStreamer.HwBuffers.GstD3D11Buf")

namespace {

using GstD3DVideoBufferCommon::kMaxPlanes;
using GstD3DVideoBufferCommon::MapDiagnostics;
using D3D11FrameTextures = GstD3DVideoBufferCommon::FrameTextures<ID3D11Texture2D>;

MapDiagnostics s_diag;

/// Copy the visible part of a subresource slice into a frame-owned ID3D11Texture2D for QRhi; returns the staging
/// texture (caller owns the ref) or nullptr. Does NOT flush — the caller flushes once after all planes.
ID3D11Texture2D* copyToStaging(ID3D11Texture2D* tex, guint subIdx, int planeIdx, const D3D11_TEXTURE2D_DESC& srcDesc,
                               QSize planeSize, GstD3D11Memory* d3dmem)
{
    ID3D11Device* d3dDev = gst_d3d11_device_get_device_handle(d3dmem->device);
    ID3D11DeviceContext* d3dCtx = gst_d3d11_device_get_device_context_handle(d3dmem->device);
    D3D11_TEXTURE2D_DESC dstDesc = srcDesc;
    dstDesc.ArraySize = 1;
    dstDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    dstDesc.MiscFlags = 0;
    dstDesc.MipLevels = 1;
    dstDesc.Width = static_cast<UINT>(planeSize.width());
    dstDesc.Height = static_cast<UINT>(planeSize.height());
    // A global size-keyed ring can overwrite a texture still displayed by another stream.
    // Keep the copy owned by this frame until Qt releases its buffer and texture bundle.
    ID3D11Texture2D* stagingTex = nullptr;
    if (FAILED(d3dDev->CreateTexture2D(&dstDesc, nullptr, &stagingTex))) {
        QGC_HW_WARN_ONCE(
            GstD3D11Log, s_diag.loggedTextureCreateFail,
            "resolve: CreateTexture2D for frame copy failed (plane=" << planeIdx << " subresource=" << subIdx << ")");
        return nullptr;
    }
    // The immediate ID3D11DeviceContext is not free-threaded; gst-d3d11 device-lock contract requires this guard.
    gst_d3d11_device_lock(d3dmem->device);
    const D3D11_BOX sourceBox{0, 0, 0, dstDesc.Width, dstDesc.Height, 1};
    d3dCtx->CopySubresourceRegion(stagingTex, 0, 0, 0, 0, tex, subIdx, &sourceBox);
    gst_d3d11_device_unlock(d3dmem->device);
    return stagingTex;
}

}  // namespace

void GstD3D11VideoBuffer::resetCachedState() noexcept
{
    s_diag.reset();
}

namespace {
struct D3D11CacheResetRegistrar
{
    D3D11CacheResetRegistrar() { GstContextBridgeRegistry::registerCacheReset(&GstD3D11VideoBuffer::resetCachedState); }
};

const D3D11CacheResetRegistrar s_d3d11CacheResetRegistrar;
}  // namespace

GstD3D11VideoBuffer::GstD3D11VideoBuffer(GstSample* sample, const GstVideoInfo& videoInfo,
                                         const QVideoFrameFormat& format)
    : GstHwVideoBuffer(QVideoFrame::RhiTextureHandle, sample, videoInfo, format)
{
    // Device guard + slice copy + flush on the streaming thread; mapTextures (render thread) only imports resolved
    // textures.
    resolvePlaneResources();
}

GstD3D11VideoBuffer::~GstD3D11VideoBuffer()
{
    for (int i = 0; i < _resolvedCount; ++i) {
        if (_textures[i])
            _textures[i]->Release();
    }
}

bool GstD3D11VideoBuffer::validatePlaneHandles() const
{
    // Constructor-time resolve failed (device mismatch, null resource, staging copy) — let the factory demote to CPU.
    if (!_resolved) {
        return false;
    }
    return validatePlanes([](GstMemory* mem) {
        if (!mem || !gst_is_d3d11_memory(mem))
            return false;
        // Cheap field read; confirms the wrapper actually backs an ID3D11Texture2D.
        return gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem)) != nullptr;
    });
}

void GstD3D11VideoBuffer::resolvePlaneResources()
{
    GstBuffer* buffer = _sample ? gst_sample_get_buffer(_sample) : nullptr;
    if (!buffer)
        return;

    const int memCount = (std::min)(int(gst_buffer_n_memory(buffer)), kMaxPlanes);
    GstD3DVideoBufferCommon::PlaneResourceGuard<ID3D11Texture2D> guard;
    GstD3D11Device* copyDevice = nullptr;

    for (int i = 0; i < memCount; ++i) {
        GstMemory* mem = gst_buffer_peek_memory(buffer, i);
        if (!mem || !gst_is_d3d11_memory(mem)) {
            QGC_HW_WARN_ONCE(GstD3D11Log, s_diag.loggedNonD3DMemory,
                             "resolve: plane" << i << "memory is not GstD3D11Memory (allocator="
                                              << (mem && mem->allocator ? mem->allocator->mem_type : "null") << ")");
            return;
        }
        // Device guard on plane 0: gst-d3d11 may land on an isolated device when NEED_CONTEXT was preempted; sampling a
        // foreign-device texture from QRhi corrupts silently.
        if (i == 0) {
            // currentDevice() is transfer-full — unref both branches to avoid UAF after reset().
            GstD3D11Device* bridgeDev = GstD3D11ContextBridge::currentDevice();
            GstD3D11Device* bufDev = GST_D3D11_MEMORY_CAST(mem)->device;
            if (bridgeDev && bufDev != bridgeDev) {
                const gint64 bridgeLuid = GstD3DContextBridgeCommon::readAdapterLuid(bridgeDev);
                const gint64 bufLuid = GstD3DContextBridgeCommon::readAdapterLuid(bufDev);
                gst_object_unref(bridgeDev);
                QGC_HW_WARN_ONCE(GstD3D11Log, s_diag.loggedDeviceMismatch,
                                 "resolve: GstD3D11Memory on foreign device (bridge LUID="
                                     << bridgeLuid << "buffer LUID=" << bufLuid
                                     << "); bridge missed NEED_CONTEXT race — rejecting frame");
                return;
            }
            if (bridgeDev)
                gst_object_unref(bridgeDev);
        }
        ID3D11Texture2D* tex =
            reinterpret_cast<ID3D11Texture2D*>(gst_d3d11_memory_get_resource_handle(GST_D3D11_MEMORY_CAST(mem)));
        if (!tex) {
            QGC_HW_WARN_ONCE(GstD3D11Log, s_diag.loggedNullResource,
                             "resolve: gst_d3d11_memory_get_resource_handle returned null for plane" << i);
            return;
        }
        // QRhi::createFrom has no subresource selector — copy array slices here on the streaming thread.
        const guint subIdx = gst_d3d11_memory_get_subresource_index(GST_D3D11_MEMORY_CAST(mem));
        D3D11_TEXTURE2D_DESC srcDesc = {};
        tex->GetDesc(&srcDesc);
        const QSize planeSize =
            i == 0 ? _format.frameSize()
                   : QSize(GST_VIDEO_INFO_COMP_WIDTH(&_videoInfo, i), GST_VIDEO_INFO_COMP_HEIGHT(&_videoInfo, i));
        if (planeSize.isEmpty() || srcDesc.Width < static_cast<UINT>(planeSize.width()) ||
            srcDesc.Height < static_cast<UINT>(planeSize.height())) {
            return;
        }
        // Hardware decoders can pad beyond the negotiated picture (e.g. 1080 -> 1152 rows).
        // Copy only the picture so Qt samples no padding and retains the original display aspect ratio.
        const bool padded = srcDesc.Width != static_cast<UINT>(planeSize.width()) ||
                            srcDesc.Height != static_cast<UINT>(planeSize.height());
        if (subIdx > 0 || srcDesc.ArraySize > 1 || padded) {
            ID3D11Texture2D* stagingTex = copyToStaging(tex, subIdx, i, srcDesc, planeSize, GST_D3D11_MEMORY_CAST(mem));
            if (!stagingTex) {
                return;
            }
            copyDevice = GST_D3D11_MEMORY_CAST(mem)->device;
            guard.handles[i] = stagingTex;
        } else {
            tex->AddRef();
            guard.handles[i] = tex;
        }
        guard.owned = i + 1;
    }

    // Single flush for every staged slice instead of one per plane: QRhi must see the copies in the immediate queue.
    if (copyDevice) {
        ID3D11DeviceContext* d3dCtx = gst_d3d11_device_get_device_context_handle(copyDevice);
        gst_d3d11_device_lock(copyDevice);
        d3dCtx->Flush();
        gst_d3d11_device_unlock(copyDevice);
    }

    _textures = guard.handles;
    _resolvedCount = memCount;
    _resolved = true;
    guard.commit();
}

QVideoFrameTexturesUPtr GstD3D11VideoBuffer::mapTextures(QRhi& rhi, QVideoFrameTexturesUPtr& old)
{
    // GstD3D11ContextBridge must be primed; without a shared device createFrom() silently renders garbage.
    GstBuffer* buffer = nullptr;
    if (!checkMapPreconditions(rhi, static_cast<int>(QRhi::D3D11), GstD3D11Log(), s_diag, buffer)) {
        return GstHwPathTelemetry::fail(HwVideoBufferPath::D3D11);
    }
    if (!_resolved) {
        return GstHwPathTelemetry::fail(HwVideoBufferPath::D3D11);
    }

    return GstD3DVideoBufferCommon::mapResolvedTextures(
        *this, rhi, old, HwVideoBufferPath::D3D11, _textures, _resolvedCount, _format.frameSize(),
        _format.pixelFormat(), s_diag.loggedFirstSuccess, s_diag.loggedTextureCreateFail, GstD3D11Log, "D3D11");
}

#endif  // Q_OS_WIN && QGC_HAS_GST_D3D11_GPU_PATH
