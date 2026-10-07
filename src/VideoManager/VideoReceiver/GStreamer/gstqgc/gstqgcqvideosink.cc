#include "gstqgcqvideosink.h"

#include <QtCore/QMetaObject>
#include <QtCore/QMutexLocker>
#include <QtCore/QPointer>
#include <QtMultimedia/QVideoFrame>
#include <QtMultimedia/QVideoFrameFormat>
#include <QtMultimedia/QVideoSink>
#include <gst/video/video-info.h>
#include <gst/video/video.h>

#include "GStreamerFrameMap.h"
#include "GstQgcAllocation.h"
#include "HwBuffers/dmabuf/GstDmaDrmCaps.h"
#include "QGCLoggingCategory.h"
#include "gstqgcelements.h"
#if GST_CHECK_VERSION(1, 24, 0)
#include <gst/video/video-info-dma.h>
#endif

#include <atomic>
#include <memory>

QGC_LOGGING_CATEGORY(GstQgcQVideoSinkLog, "Video.GStreamer.QgcQVideoSink")

#define GST_CAT_DEFAULT gst_qgc_debug

namespace {

// Queued callbacks keep this state alive, not the Gst element or an individual frame.
struct FrameDeliveryState
{
    QMutex mutex;
    QVideoFrame pendingFrame;
    // Epoch rejects in-flight mapping after a stream reset; binding identifies the wake's QObject target.
    quint64 epoch{0};
    quint64 binding{0};
    bool refreshQueued{false};
    bool acceptingFrames{true};
    std::atomic<quint64> inputFrames{0};
    std::atomic<quint64> droppedFrames{0};
    std::atomic<quint64> deliveredFrames{0};
};

/// Non-POD state hung off the GObject instance via `priv`. Owned, new'd in instance_init,
/// delete'd in finalize.
struct PrivState
{
    QVideoFrameFormat format;
    // Written under GST_OBJECT_LOCK from the GUI thread, snapshotted by show_frame.
    // Default (gpuEnabled=false) keeps the CPU memcpy path until the controller wires it.
    HwVideoBufferContext hw_context = {};
    std::shared_ptr<FrameDeliveryState> delivery = std::make_shared<FrameDeliveryState>();
    std::atomic<int64_t> last_pts_ns{-1};
    std::atomic<quint64> consecutive_map_failures{0};  // sustained run escalates show_frame to error
    // Negotiated caps held from set_caps; avoids per-frame allocation and preserves DRM modifiers.
    GstCaps* cached_caps{nullptr};
#if defined(QGC_HAS_ANY_GPU_PATH)
    // Per-caps-epoch resolved-path cache; reset on set_caps so a format change re-runs path selection.
    HwResolvedPathCache resolved_path_cache = {};
#endif
};

inline PrivState* priv_of(GstQgcQVideoSink* self)
{
    return static_cast<PrivState*>(self->priv);
}

/// Snapshot the QVideoSink* under GST_OBJECT_LOCK as a QPointer: the sink may be destroyed
/// on its owner thread between snapshot and push.
QPointer<QVideoSink> snapshot_sink(GstQgcQVideoSink* self, HwVideoBufferContext& hwOut, quint64& epoch)
{
    QPointer<QVideoSink> out;
    GST_OBJECT_LOCK(self);
    PrivState* p = priv_of(self);
    {
        QMutexLocker locker(&p->delivery->mutex);
        if (p->delivery->acceptingFrames)
            out = static_cast<QVideoSink*>(self->qvideosink);
        epoch = p->delivery->epoch;
    }
    hwOut = p->hw_context;
    GST_OBJECT_UNLOCK(self);
    return out;
}

// Caller holds GST_OBJECT_LOCK. The discarded frame must outlive both locks: releasing a
// GPU frame may enter the decoder/device, which must never run inside the delivery locks.
void invalidate_pending_locked(PrivState* p, QVideoFrame& discarded, bool bindingChanged = false)
{
    QMutexLocker locker(&p->delivery->mutex);
    ++p->delivery->epoch;
    if (bindingChanged) {
        ++p->delivery->binding;
        p->delivery->refreshQueued = false;
    }
    p->delivery->pendingFrame.swap(discarded);
    if (discarded.isValid())
        p->delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
    p->last_pts_ns.store(-1, std::memory_order_relaxed);
}

void reset_stream_delivery(GstQgcQVideoSink* self, bool acceptingFrames)
{
    QVideoFrame discarded;
    GST_OBJECT_LOCK(self);
    PrivState* p = priv_of(self);
    invalidate_pending_locked(p, discarded);
    {
        QMutexLocker locker(&p->delivery->mutex);
        p->delivery->acceptingFrames = acceptingFrames;
    }
    GST_OBJECT_UNLOCK(self);
}

void push_frame_queued(GstQgcQVideoSink* self, QVideoFrame&& frame, quint64 epoch, int64_t pts)
{
    QVideoFrame rejected;
    GST_OBJECT_LOCK(self);
    PrivState* p = priv_of(self);
    const auto delivery = p->delivery;
    {
        QMutexLocker locker(&delivery->mutex);
        QVideoSink* sink = static_cast<QVideoSink*>(self->qvideosink);
        if (!sink || !g_atomic_int_get(&self->active) || !delivery->acceptingFrames || epoch != delivery->epoch) {
            delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
        } else {
            delivery->pendingFrame.swap(frame);
            if (frame.isValid())
                delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);

            bool queued = true;
            if (!delivery->refreshQueued) {
                delivery->refreshQueued = true;
                const quint64 binding = delivery->binding;
                // Posting under GST_OBJECT_LOCK keeps the target alive until invokeMethod;
                // the controller clears the property from its destroyed handler under that lock.
                queued = QMetaObject::invokeMethod(
                    sink,
                    [delivery, sink, binding]() {
                        QVideoFrame current;
                        {
                            QMutexLocker locker(&delivery->mutex);
                            if (binding != delivery->binding)
                                return;
                            delivery->refreshQueued = false;
                            delivery->pendingFrame.swap(current);
                        }
                        if (!current.isValid())
                            return;
                        // QVideoSink and the renderer keep their own frame references. Neither
                        // delivery lock is held while Qt replaces/releases its previous frame.
                        sink->setVideoFrame(current);
                        const quint64 delivered = delivery->deliveredFrames.fetch_add(1, std::memory_order_relaxed) + 1;
                        if (delivered == 1) {
                            qCInfo(GstQgcQVideoSinkLog) << "first frame delivered" << current.size();
                        } else if ((delivered % 300) == 0) {
                            qCDebug(GstQgcQVideoSinkLog)
                                << "frame flow: delivered=" << delivered
                                << "input=" << delivery->inputFrames.load(std::memory_order_relaxed)
                                << "dropped=" << delivery->droppedFrames.load(std::memory_order_relaxed);
                        }
                    },
                    Qt::QueuedConnection);
                if (!queued) {
                    delivery->refreshQueued = false;
                    delivery->pendingFrame.swap(rejected);
                    delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (queued && pts >= 0)
                p->last_pts_ns.store(pts, std::memory_order_release);
        }
    }
    GST_OBJECT_UNLOCK(self);
    // frame now owns a replaced pending frame, or the rejected input. Both it and rejected
    // are released after the locks; the queued functor contains no QVideoFrame payload.
}

}  // namespace

/// Pad template — sink accepts any video caps. The downstream conversion
/// (videoconvert/glupload) already happens upstream in qgcvideosinkbin.
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

enum
{
    PROP_0,
    PROP_QVIDEOSINK,
    PROP_ACTIVE,
    PROP_GPU_ZEROCOPY,
    PROP_FRAMES_INPUT,
    PROP_FRAMES_DROPPED,
    PROP_FRAMES_DELIVERED,
};

G_DEFINE_FINAL_TYPE(GstQgcQVideoSink, gst_qgc_q_video_sink, GST_TYPE_VIDEO_SINK)
GST_ELEMENT_REGISTER_DEFINE(qgcqvideosink, "qgcqvideosink", GST_RANK_NONE, GST_TYPE_QGC_Q_VIDEO_SINK)

static void gst_qgc_q_video_sink_init(GstQgcQVideoSink* self)
{
    self->qvideosink = nullptr;
    self->active = TRUE;
    self->gpu_zerocopy = FALSE;
    self->caps_valid = FALSE;
    gst_video_info_init(&self->video_info);
    self->priv = new PrivState();
    // sync=FALSE: drone video is "as fast as decoded"; GstBaseSink clock-sync would
    // stall on live RTSP. async=FALSE skips preroll wait so caps changes don't stall.
    gst_base_sink_set_sync(GST_BASE_SINK(self), FALSE);
    gst_base_sink_set_async_enabled(GST_BASE_SINK(self), FALSE);
    // Don't retain the last buffer: it would pin an upstream pool slot for the stream's lifetime.
    gst_base_sink_set_last_sample_enabled(GST_BASE_SINK(self), FALSE);
}

void gst_qgc_q_video_sink_set_hw_context(GstQgcQVideoSink* self, const HwVideoBufferContext& ctx)
{
    if (!self)
        return;
    GST_OBJECT_LOCK(self);
    priv_of(self)->hw_context = ctx;
    GST_OBJECT_UNLOCK(self);
}

static void gst_qgc_q_video_sink_finalize(GObject* obj)
{
    GstQgcQVideoSink* self = GST_QGC_Q_VIDEO_SINK(obj);
    QVideoFrame discarded;
    GST_OBJECT_LOCK(self);
    invalidate_pending_locked(priv_of(self), discarded, true);
    self->qvideosink = nullptr;
    GST_OBJECT_UNLOCK(self);
    gst_clear_caps(&priv_of(self)->cached_caps);
    delete priv_of(self);
    self->priv = nullptr;
    G_OBJECT_CLASS(gst_qgc_q_video_sink_parent_class)->finalize(obj);
}

static void gst_qgc_q_video_sink_set_property(GObject* obj, guint id, const GValue* val, GParamSpec* pspec)
{
    GstQgcQVideoSink* self = GST_QGC_Q_VIDEO_SINK(obj);
    QVideoFrame discarded;
    GST_OBJECT_LOCK(self);
    switch (id) {
        case PROP_QVIDEOSINK: {
            gpointer raw = g_value_get_pointer(val);
            if (self->qvideosink != raw) {
                invalidate_pending_locked(priv_of(self), discarded, true);
                self->qvideosink = raw;
            }
            break;
        }
        case PROP_ACTIVE: {
            // Read lock-free on the streaming thread (show_frame); publish atomically.
            const gboolean active = g_value_get_boolean(val);
            if (!active && g_atomic_int_get(&self->active))
                invalidate_pending_locked(priv_of(self), discarded);
            g_atomic_int_set(&self->active, active);
            break;
        }
        case PROP_GPU_ZEROCOPY:
            self->gpu_zerocopy = g_value_get_boolean(val);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
            break;
    }
    GST_OBJECT_UNLOCK(self);
}

static void gst_qgc_q_video_sink_get_property(GObject* obj, guint id, GValue* val, GParamSpec* pspec)
{
    GstQgcQVideoSink* self = GST_QGC_Q_VIDEO_SINK(obj);
    GST_OBJECT_LOCK(self);
    switch (id) {
        case PROP_QVIDEOSINK:
            g_value_set_pointer(val, self->qvideosink);
            break;
        case PROP_ACTIVE:
            g_value_set_boolean(val, self->active);
            break;
        case PROP_GPU_ZEROCOPY:
            g_value_set_boolean(val, self->gpu_zerocopy);
            break;
        case PROP_FRAMES_INPUT:
            g_value_set_uint64(val, priv_of(self)->delivery->inputFrames.load(std::memory_order_relaxed));
            break;
        case PROP_FRAMES_DROPPED:
            g_value_set_uint64(val, priv_of(self)->delivery->droppedFrames.load(std::memory_order_relaxed));
            break;
        case PROP_FRAMES_DELIVERED:
            g_value_set_uint64(val, priv_of(self)->delivery->deliveredFrames.load(std::memory_order_relaxed));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
            break;
    }
    GST_OBJECT_UNLOCK(self);
}

static gboolean gst_qgc_q_video_sink_set_caps(GstBaseSink* bsink, GstCaps* caps)
{
    GstQgcQVideoSink* self = GST_QGC_Q_VIDEO_SINK(bsink);
    PrivState* p = priv_of(self);

    GstVideoInfo parsedInfo = {};
    if (!GstHw::dmaDrmAwareVideoInfo(caps, &parsedInfo)) {
        qCWarning(GstQgcQVideoSinkLog) << "set_caps: failed to parse video info from caps";
        return FALSE;
    }

    const QVideoFrameFormat::PixelFormat pixelFormat = toQtPixelFormat(GST_VIDEO_INFO_FORMAT(&parsedInfo));
    if (pixelFormat == QVideoFrameFormat::Format_Invalid) {
        qCWarning(GstQgcQVideoSinkLog) << "set_caps: unsupported video format"
                                       << gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&parsedInfo));
        return FALSE;
    }

    const int w = GST_VIDEO_INFO_WIDTH(&parsedInfo);
    const int h = GST_VIDEO_INFO_HEIGHT(&parsedInfo);
    if (w <= 0 || h <= 0) {
        qCWarning(GstQgcQVideoSinkLog) << "set_caps: invalid dimensions" << w << "x" << h;
        return FALSE;
    }

    QVideoFrameFormat fmt(QSize(w, h), pixelFormat);
    applyColorimetry(fmt, parsedInfo, caps);
    const int fpsN = GST_VIDEO_INFO_FPS_N(&parsedInfo);
    const int fpsD = GST_VIDEO_INFO_FPS_D(&parsedInfo);
    if (fpsN > 0 && fpsD > 0) {
        fmt.setStreamFrameRate(static_cast<qreal>(fpsN) / static_cast<qreal>(fpsD));
    }

    QVideoFrame discarded;
    GST_OBJECT_LOCK(self);
    invalidate_pending_locked(p, discarded);
    GST_OBJECT_UNLOCK(self);
    self->video_info = parsedInfo;
    p->format = std::move(fmt);
    gst_clear_caps(&p->cached_caps);
    // Hold the negotiated caps verbatim: rebuilding from video_info drops DRM modifiers and other
    // negotiated fields the downstream frame mapping relies on.
    p->cached_caps = gst_caps_ref(caps);
#if defined(QGC_HAS_ANY_GPU_PATH)
    p->resolved_path_cache = HwResolvedPathCache{};
#endif
    // caps_valid is read lock-free on the streaming thread (show_frame); publish atomically.
    g_atomic_int_set(&self->caps_valid, TRUE);

    // Posted on the bus so the controller can mirror negotiation state into Q_PROPERTY.
    {
        gchar* fmtName = g_strdup(gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&parsedInfo)));
        GstStructure* s = gst_structure_new("qgc-caps-info", "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, "format",
                                            G_TYPE_STRING, fmtName, nullptr);
        gst_element_post_message(GST_ELEMENT(self), gst_message_new_element(GST_OBJECT(self), s));
        g_free(fmtName);
    }

    qCInfo(GstQgcQVideoSinkLog).noquote()
        << "set_caps: format=" << gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&parsedInfo)) << w << "x" << h
        << "pixfmt=" << int(pixelFormat);
    return TRUE;
}

static gboolean gst_qgc_q_video_sink_start(GstBaseSink* bsink)
{
    GstBaseSinkClass* parentClass = GST_BASE_SINK_CLASS(gst_qgc_q_video_sink_parent_class);
    if (parentClass->start && !parentClass->start(bsink))
        return FALSE;
    reset_stream_delivery(GST_QGC_Q_VIDEO_SINK(bsink), true);
    return TRUE;
}

static gboolean gst_qgc_q_video_sink_stop(GstBaseSink* bsink)
{
    reset_stream_delivery(GST_QGC_Q_VIDEO_SINK(bsink), false);
    GstBaseSinkClass* parentClass = GST_BASE_SINK_CLASS(gst_qgc_q_video_sink_parent_class);
    return !parentClass->stop || parentClass->stop(bsink);
}

static gboolean gst_qgc_q_video_sink_event(GstBaseSink* bsink, GstEvent* event)
{
    GstQgcQVideoSink* self = GST_QGC_Q_VIDEO_SINK(bsink);
    const GstEventType type = GST_EVENT_TYPE(event);
    if (type == GST_EVENT_FLUSH_START) {
        reset_stream_delivery(self, false);
    } else if (type == GST_EVENT_SEGMENT) {
        // A seek/restart may begin at a lower PTS without changing negotiated caps.
        QVideoFrame discarded;
        GST_OBJECT_LOCK(self);
        invalidate_pending_locked(priv_of(self), discarded);
        GST_OBJECT_UNLOCK(self);
    }
    GstBaseSinkClass* parentClass = GST_BASE_SINK_CLASS(gst_qgc_q_video_sink_parent_class);
    const gboolean handled = parentClass->event ? parentClass->event(bsink, event) : FALSE;
    if (type == GST_EVENT_FLUSH_STOP && handled)
        reset_stream_delivery(self, true);
    return handled;
}

// Sustained run of map failures (not a transient hiccup) means the import is broken — tear down + restart.
constexpr quint64 kMaxConsecutiveMapFailures = 120;

static GstFlowReturn gst_qgc_q_video_sink_show_frame(GstVideoSink* vsink, GstBuffer* buf)
{
    GstQgcQVideoSink* self = GST_QGC_Q_VIDEO_SINK(vsink);
    PrivState* p = priv_of(self);

    p->delivery->inputFrames.fetch_add(1, std::memory_order_relaxed);

    if (!g_atomic_int_get(&self->caps_valid)) {
        // Should never happen — GstBaseSink calls set_caps before show_frame.
        return GST_FLOW_NOT_NEGOTIATED;
    }
    if (!g_atomic_int_get(&self->active)) {
        p->delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
        return GST_FLOW_OK;  // drop silently — controller drives the active flag
    }

    HwVideoBufferContext hwCtx;
    quint64 epoch = 0;
    QPointer<QVideoSink> sink = snapshot_sink(self, hwCtx, epoch);
    if (!sink) {
        p->delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
        return GST_FLOW_OK;  // no destination yet; drop
    }

    // PTS regression guard ahead of mapping: a regressed timestamp wedges QVideoOutput's internal
    // advance, and checking first avoids a wasted full-frame map on a buffer we'd drop anyway.
    // last_pts_ns advances only when a valid frame enters the current epoch's pending slot,
    // so a transient map failure or a concurrent rebind cannot advance the guard.
    const bool hasPts = GST_BUFFER_PTS_IS_VALID(buf);
    const int64_t pts = hasPts ? static_cast<int64_t>(GST_BUFFER_PTS(buf)) : -1;
    if (hasPts) {
        const int64_t lastPts = p->last_pts_ns.load(std::memory_order_acquire);
        if (lastPts >= 0 && pts < lastPts) {
            p->delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
            return GST_FLOW_OK;
        }
    }

    // Build a cropped format copy only when crop meta is present (rare); otherwise pass
    // p->format by reference to avoid a per-frame QVideoFrameFormat refcount bump.
    QVideoFrameFormat croppedFmt;
    const bool hasCrop = (gst_buffer_get_video_crop_meta(buf) != nullptr);
    if (hasCrop) {
        croppedFmt = applyCropMeta(p->format, buf);
    }
    MappedFrame mapped =
        mapSampleToFrame(buf, p->cached_caps, self->video_info, hasCrop ? croppedFmt : p->format, hwCtx,
#if defined(QGC_HAS_ANY_GPU_PATH)
                         &p->resolved_path_cache);
#else
                         nullptr);
#endif
    if (!mapped.frame.isValid()) {
        p->delivery->droppedFrames.fetch_add(1, std::memory_order_relaxed);
        const quint64 c = p->consecutive_map_failures.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((c & 0x3F) == 1) {
            qCWarning(GstQgcQVideoSinkLog) << "show_frame: mapping failed, consecutive=" << c;
        }
        if (c >= kMaxConsecutiveMapFailures) {
            qCWarning(GstQgcQVideoSinkLog) << "show_frame:" << c << "consecutive map failures — erroring out";
            return GST_FLOW_ERROR;
        }
        return GST_FLOW_OK;  // drop transient failure, keep the stream alive
    }
    p->consecutive_map_failures.store(0, std::memory_order_relaxed);

    // Stream orientation is always IDENTITY here; tag-driven orientation lives in the
    // controller (GST_TAG_IMAGE_ORIENTATION). Per-buffer GstVideoOrientationMeta still wins.
    applyOrientationAndTiming(mapped.frame, buf, static_cast<int>(GST_VIDEO_ORIENTATION_IDENTITY));

    // HwPathTelemetry describes mapped paths. The per-element frames-delivered counter
    // advances separately when the GUI consumes a frame, after coalescing.
    if (mapped.source == MappedFrame::Source::Cpu) {
        GstHwPathTelemetry::recordDelivered(HwVideoBufferPath::None);
#if defined(QGC_HAS_ANY_GPU_PATH)
        // Stream started HW-capable but this frame fell back to CPU — record the demotion once per epoch.
        if (mapped.demoted && !p->resolved_path_cache.demotionRecorded) {
            p->resolved_path_cache.demotionRecorded = true;
            GstHwPathTelemetry::recordStreamDemotion(mapped.gpuPath);
        }
#endif
    }
#if defined(QGC_HAS_ANY_GPU_PATH)
    else {
        GstHwPathTelemetry::recordDelivered(mapped.gpuPath);
    }
#endif

    push_frame_queued(self, std::move(mapped.frame), epoch, pts);
    return GST_FLOW_OK;
}

// Add qgcqvideosink's consumed metas + min-buffer pool hint, then chain so GstBaseSink's default
// allocation bookkeeping still runs. Replaces the former QUERY_DOWNSTREAM pad probe for ALLOCATION.
static gboolean gst_qgc_q_video_sink_propose_allocation(GstBaseSink* bsink, GstQuery* query)
{
    GstQgc::populateAllocationQuery(query);
    // GstBaseSink/GstVideoSink install no default propose_allocation vmethod, so the parent pointer
    // is NULL for a direct subclass; chaining unconditionally would call 0x0 on the first ALLOCATION query.
    GstBaseSinkClass* parentClass = GST_BASE_SINK_CLASS(gst_qgc_q_video_sink_parent_class);
    if (parentClass->propose_allocation) {
        return parentClass->propose_allocation(bsink, query);
    }
    return TRUE;
}

static void gst_qgc_q_video_sink_class_init(GstQgcQVideoSinkClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass* element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass* basesink_class = GST_BASE_SINK_CLASS(klass);
    GstVideoSinkClass* videosink_class = GST_VIDEO_SINK_CLASS(klass);

    gobject_class->set_property = gst_qgc_q_video_sink_set_property;
    gobject_class->get_property = gst_qgc_q_video_sink_get_property;
    gobject_class->finalize = gst_qgc_q_video_sink_finalize;

    g_object_class_install_property(
        gobject_class, PROP_QVIDEOSINK,
        g_param_spec_pointer("qvideosink", "QVideoSink target",
                             "QVideoSink* to push frames into. Caller-owned; element never unrefs.",
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_ACTIVE,
        g_param_spec_boolean("active", "Active",
                             "When FALSE, show_frame drops buffers instead of pushing to the QVideoSink.", TRUE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_GPU_ZEROCOPY,
        g_param_spec_boolean("gpu-zerocopy", "GPU zero-copy",
                             "Attempt GPU-zerocopy mapping in show_frame; false forces CPU memcpy.", FALSE,
                             (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FRAMES_INPUT,
        g_param_spec_uint64("frames-input", "Frames input", "Total buffers seen by show_frame, including drops.", 0,
                            G_MAXUINT64, 0, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FRAMES_DROPPED,
        g_param_spec_uint64("frames-dropped", "Frames dropped",
                            "Buffers rejected by show_frame (inactive sink, missing QVideoSink, "
                            "PTS regression, or map failure), superseded pending frames, and stream/binding resets. "
                            "Detailed map failures are tracked separately "
                            "via GstHwPathTelemetry::peekMapFailureCount.",
                            0, G_MAXUINT64, 0, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FRAMES_DELIVERED,
        g_param_spec_uint64("frames-delivered", "Frames delivered",
                            "Frames consumed by the QVideoSink on its owner thread after pending-frame coalescing. "
                            "Per-element — the GUI controller reads this for the QML frameCount.",
                            0, G_MAXUINT64, 0, (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class, "QGC QVideoSink", "Sink/Video",
                                          "Pushes decoded GstVideoFrames into a Qt QVideoSink",
                                          "QGroundControl <https://qgroundcontrol.com/>");
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    basesink_class->set_caps = gst_qgc_q_video_sink_set_caps;
    basesink_class->start = gst_qgc_q_video_sink_start;
    basesink_class->stop = gst_qgc_q_video_sink_stop;
    basesink_class->event = gst_qgc_q_video_sink_event;
    basesink_class->propose_allocation = gst_qgc_q_video_sink_propose_allocation;
    videosink_class->show_frame = gst_qgc_q_video_sink_show_frame;
}
