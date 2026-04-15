#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif

#include <vector>
#include <string>
#include <iostream>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/va/gstva.h>
#include <va/va.h>

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/core/va_intel.hpp>
#include <opencv2/core/ocl.hpp>

// DL Streamer
#include <dlstreamer/gst/videoanalytics/video_frame.h>

#define PACKAGE "thzsilhouette"
#define GST_TYPE_THZ_SILHOUETTE (thz_silhouette_get_type())
G_DECLARE_FINAL_TYPE(ThzSilhouette, thz_silhouette, THZ, SILHOUETTE, GstBaseTransform)

struct _ThzSilhouette {
    GstBaseTransform parent;
    gchar *target_label;
    GstVideoInfo vinfo;
    VADisplay va_display;
    gboolean va_initialized;
    cv::Mat overlay_cpu;
    cv::UMat overlay_gpu;
};

G_DEFINE_TYPE(ThzSilhouette, thz_silhouette, GST_TYPE_BASE_TRANSFORM);

/**
 * Helper to grab VADisplay directly from the buffer's memory
 * Updated with compatible function names based on your compiler errors.
 */
static VADisplay get_va_display_from_buffer(GstBuffer *buf) {
    GstMemory *mem = gst_buffer_peek_memory(buf, 0);
    // Use the generic type check if the specific macro is missing
    if (!mem || !gst_memory_is_type(mem, "VAMemory")) return NULL;
    
    // Using 'peek' instead of 'get' as per compiler suggestion
    GstVaDisplay *gst_display = gst_va_memory_peek_display(mem);
    if (!gst_display) return NULL;
    
    // Using 'get_va_dpy' which is the common naming in many Gst-VA versions
    return gst_va_display_get_va_dpy(gst_display);
}

static GstFlowReturn thz_silhouette_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    ThzSilhouette *self = THZ_SILHOUETTE(trans);
    if (self->vinfo.width <= 0) return GST_FLOW_OK;

    // Identify the Surface and Display
    VASurfaceID surface_id = gst_va_buffer_get_surface(buf);
    VADisplay current_display = get_va_display_from_buffer(buf);

    if (surface_id == VA_INVALID_SURFACE || !current_display) {
        return GST_FLOW_OK; 
    }

    // Re-init OpenCV if the hardware display context changes
    if (!self->va_initialized || self->va_display != current_display) {
        try {
            cv::va_intel::ocl::initializeContextFromVA(current_display, true);
            self->va_display = current_display;
            self->va_initialized = TRUE;
            std::cout << "[THZ] VA/OCL Interop Initialized" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "[THZ] Interop Error: " << e.what() << std::endl;
            return GST_FLOW_OK;
        }
    }

    try {
        GVA::VideoFrame video_frame(buf, &self->vinfo);
        bool has_render = false;

        if (self->overlay_cpu.empty())
            self->overlay_cpu.create(self->vinfo.height, self->vinfo.width, CV_8UC3);
        self->overlay_cpu.setTo(cv::Scalar(0, 0, 0));

        for (auto &roi : video_frame.regions()) {
            if (roi.label() != self->target_label) continue;
            for (auto &tensor : roi.tensors()) {
                if (tensor.format() == "segmentation_mask") {
                    has_render = true;
                    auto dims = tensor.dims();
                    auto rect = roi.rect();
                    cv::Mat mask_mat(dims[1], dims[0], CV_32F, (void*)tensor.data<float>().data());
                    mask_mat = (mask_mat > 0.3);
                    cv::Mat bin;
                    cv::resize(mask_mat, bin, cv::Size(rect.w, rect.h));
                    bin.convertTo(bin, CV_8U);
                    cv::Rect bbox(rect.x, rect.y, rect.w, rect.h);
                    self->overlay_cpu(bbox).setTo(cv::Scalar(0, 255, 0), bin);
                }
            }
        }

        if (has_render && self->va_initialized) {
            // Synchronize surface access
            vaSyncSurface(self->va_display, surface_id);

            {
                cv::UMat va_frame;
                cv::va_intel::convertFromVASurface(self->va_display, surface_id, 
                                                  cv::Size(self->vinfo.width, self->vinfo.height), 
                                                  va_frame);

                self->overlay_cpu.copyTo(self->overlay_gpu);

                cv::UMat gray, alpha;
                cv::cvtColor(self->overlay_gpu, gray, cv::COLOR_BGR2GRAY);
                cv::threshold(gray, alpha, 1, 255, cv::THRESH_BINARY);

                // GPU Blending copy
                self->overlay_gpu.copyTo(va_frame, alpha);

                cv::va_intel::convertToVASurface(self->va_display, va_frame, surface_id, 
                                                cv::Size(self->vinfo.width, self->vinfo.height));
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "[THZ] Transform Exception: " << e.what() << std::endl;
    }

    return GST_FLOW_OK;
}

static gboolean thz_silhouette_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    ThzSilhouette *self = THZ_SILHOUETTE(trans);
    return gst_video_info_from_caps(&self->vinfo, incaps);
}

static void thz_silhouette_finalize(GObject *object) {
    ThzSilhouette *self = THZ_SILHOUETTE(object);
    g_free(self->target_label);
    G_OBJECT_CLASS(thz_silhouette_parent_class)->finalize(object);
}

static void thz_silhouette_class_init(ThzSilhouetteClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->finalize = thz_silhouette_finalize;
    base_class->transform_ip = GST_DEBUG_FUNCPTR(thz_silhouette_transform_ip);
    base_class->set_caps = GST_DEBUG_FUNCPTR(thz_silhouette_set_caps);

    gst_element_class_set_static_metadata(element_class, 
        "THZ Silhouette Overlay", "Filter/Video/Analysis", 
        "Blends DL Streamer masks into VAMemory", "Gemini");

    GstCaps *caps = gst_caps_from_string("video/x-raw(memory:VAMemory); video/x-raw");
    gst_element_class_add_pad_template(element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
}

static void thz_silhouette_init(ThzSilhouette *self) {
    self->target_label = g_strdup("person");
    self->va_display = NULL;
    self->va_initialized = FALSE;
    gst_video_info_init(&self->vinfo);
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "thzsilhouette", GST_RANK_NONE, GST_TYPE_THZ_SILHOUETTE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, thzsilhouette, "THZ Silhouette", plugin_init, "1.0", "MIT", "THZ", "https://example.com")