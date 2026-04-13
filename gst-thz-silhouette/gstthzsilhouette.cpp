#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif

// Standard C++ headers
#include <vector>
#include <string>

// GStreamer headers
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/va/gstva.h>
#include <gst/base/gstbasetransform.h>

// DL Streamer Metadata headers
// Note: Ensure these are in your include path (usually /opt/intel/dlstreamer/include)
#include <dlstreamer/gst/videoanalytics/video_frame.h>
#include <dlstreamer/gst/videoanalytics/region_of_interest.h>
#include <dlstreamer/gst/videoanalytics/tensor.h>

// Required for GST_PLUGIN_DEFINE
#define PACKAGE "thzsilhouette"

#define GST_TYPE_THZ_SILHOUETTE (thz_silhouette_get_type())
G_DECLARE_FINAL_TYPE(ThzSilhouette, thz_silhouette, THZ, SILHOUETTE, GstBaseTransform)

struct _ThzSilhouette {
    GstBaseTransform parent;
    gchar *target_label;
    GstVideoInfo vinfo;
};

G_DEFINE_TYPE(ThzSilhouette, thz_silhouette, GST_TYPE_BASE_TRANSFORM);

static GstFlowReturn thz_silhouette_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    ThzSilhouette *self = THZ_SILHOUETTE(trans);

    if (self->vinfo.width == 0) return GST_FLOW_OK;

    try {
        GVA::VideoFrame video_frame(buf, &self->vinfo);
        
        for (auto &roi : video_frame.regions()) {
            if (roi.label() == self->target_label) {
                for (auto &tensor : roi.tensors()) {
                    if (tensor.format() == "segmentation_mask") {
                        
                        std::vector<guint> dims = tensor.dims();
                        std::cout << "[THZ] Tensor Rank (Dimensions count): " << dims.size() << std::endl;
                        guint mask_w = dims[0];
                        guint mask_h = dims[1];
                        std::vector<float> mask_data = tensor.data<float>();

                        // 1. Print the size as requested
                        std::cout << "\n[THZ] Mask Size: " << mask_w << " x " << mask_h << std::endl;
                        
                        // 2. Iterate and print O for < 0.5 and X for > 0.5
                        for (guint y = 0; y < mask_h; y++) {
                            for (guint x = 0; x < mask_w; x++) {
                                float val = mask_data[y * mask_w + x];
                                
                                if (val >= 0.5f) {
                                    std::cout << "X";
                                } else {
                                    std::cout << "O";
                                }
                            }
                            std::cout << "\n"; // New line at the end of each row
                        }
                        std::cout << "------------------------------------------" << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(self, "Metadata error: %s", e.what());
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

    gst_element_class_set_metadata(element_class, 
        "THZ Silhouette", "Filter/Video", 
        "Prints DL Streamer segmentation mask metadata", "Gemini");

    GstCaps *caps = gst_caps_from_string("video/x-raw(memory:VAMemory); video/x-raw");
    gst_element_class_add_pad_template(element_class, 
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(element_class, 
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
}

static void thz_silhouette_init(ThzSilhouette *self) {
    self->target_label = g_strdup("person");
    gst_video_info_init(&self->vinfo);
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "thzsilhouette", GST_RANK_NONE, GST_TYPE_THZ_SILHOUETTE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, thzsilhouette,
    "THZ Silhouette Plugin", plugin_init, "1.0", "MIT", "THZ", "https://example.com")