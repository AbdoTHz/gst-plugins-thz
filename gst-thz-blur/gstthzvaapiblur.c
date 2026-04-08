#define GST_USE_UNSTABLE_API 1
#ifndef PACKAGE
#define PACKAGE "thzvaapiblur"
#endif
#define VERSION "1.0.0"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/va/gstva.h>
#include <gst/va/gstvaallocator.h>

#define GST_TYPE_THZ_VAAPI_BLUR (gst_thz_vaapi_blur_get_type())
G_DECLARE_FINAL_TYPE(GstThzVaapiBlur, gst_thz_vaapi_blur, GST, THZ_VAAPI_BLUR, GstBaseTransform)

struct _GstThzVaapiBlur {
    GstBaseTransform parent;
    gint blur_strength;
};

enum {
    PROP_0,
    PROP_BLUR_STRENGTH
};

G_DEFINE_TYPE(GstThzVaapiBlur, gst_thz_vaapi_blur, GST_TYPE_BASE_TRANSFORM);

static void gst_thz_vaapi_blur_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(object);
    if (prop_id == PROP_BLUR_STRENGTH) {
        self->blur_strength = g_value_get_int(value);
        g_print("Blur strength changed to: %d\n", self->blur_strength);
    }
}

static void gst_thz_vaapi_blur_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(object);
    if (prop_id == PROP_BLUR_STRENGTH) g_value_set_int(value, self->blur_strength);
}

static GstFlowReturn gst_thz_vaapi_blur_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    // PURE PASSTHROUGH
    // We just return OK. GStreamer keeps the buffer as-is.
    return GST_FLOW_OK;
}

static void gst_thz_vaapi_blur_class_init(GstThzVaapiBlurClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gst_thz_vaapi_blur_set_property;
    gobject_class->get_property = gst_thz_vaapi_blur_get_property;

    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_thz_vaapi_blur_transform_ip);

    g_object_class_install_property(gobject_class, PROP_BLUR_STRENGTH,
        g_param_spec_int("blur-strength", "Blur Strength", "0-100", 0, 100, 50, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(element_class, "THZ VAAPI Passthrough", "Filter/Effect", "Intel GPU Passthrough Test", "Gemini");

    // Using the 'VASurface' caps that your system prefers
    GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", 
        GST_PAD_SINK, GST_PAD_ALWAYS, 
        GST_STATIC_CAPS("video/x-raw(memory:VASurface), format=NV12"));
    
    GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", 
        GST_PAD_SRC, GST_PAD_ALWAYS, 
        GST_STATIC_CAPS("video/x-raw(memory:VASurface), format=NV12"));
    
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
}

static void gst_thz_vaapi_blur_init(GstThzVaapiBlur *self) {
    self->blur_strength = 50;
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "thzvaapiblur", GST_RANK_NONE, GST_TYPE_THZ_VAAPI_BLUR);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, thzvaapiblur, "VAAPI Passthrough", plugin_init, VERSION, "LGPL", "GStreamer", "https://gstreamer.net/")