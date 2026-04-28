#include <gst/gst.h>
#include <gst/video/video.h>
#include <cstdint>
#include <cstring>

/* * Plugin metadata and types
 */
#define PACKAGE "thzdeepcopy"
#define GST_TYPE_THZ_DEEP_COPY (thz_deep_copy_get_type())
G_DECLARE_FINAL_TYPE(GstThzDeepCopy, thz_deep_copy, GST, THZ_DEEP_COPY, GstElement)

struct _GstThzDeepCopy {
    GstElement element;
    GstPad *sinkpad;
    GstPad *srcpad;
};

G_DEFINE_TYPE(GstThzDeepCopy, thz_deep_copy, GST_TYPE_ELEMENT);

/* Pad Templates */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* * The Chain Function: This is where the magic happens
 */
static GstFlowReturn
thz_deep_copy_chain(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    GstThzDeepCopy *self = GST_THZ_DEEP_COPY(parent);
    
    /* 1. Perform a Deep Copy of the buffer.
     * This creates new memory for the H264 data, ensuring the 
     * upstream branch of the tee can free its memory safely. 
     */
    GstBuffer *new_buf = gst_buffer_copy_deep(buf);
    
    /* 2. Extract the Meta Pointer from ReferenceTimestampMeta */
    GstCaps *thz_caps = gst_caps_from_string("thz");
    GstReferenceTimestampMeta *meta = gst_buffer_get_reference_timestamp_meta(new_buf, thz_caps);

    if (meta) {
        /* Extract the structure pointer passed as a timestamp */
        GstStructure *s = (GstStructure *)((uintptr_t)meta->timestamp);
        
        if (s) {
            /* 3. Serialize GstStructure to a string for IPC transport */
            gchar *serialized_str = gst_structure_to_string(s);
            if (serialized_str) {
                size_t len = strlen(serialized_str) + 1;

                /* 4. Append the string as a new memory block to the buffer.
                 * ipcpipelinesink will serialize all memory blocks in the buffer.
                 */
                GstMemory *meta_mem = gst_allocator_alloc(NULL, len, NULL);
                GstMapInfo map;
                
                if (gst_memory_map(meta_mem, &map, GST_MAP_WRITE)) {
                    memcpy(map.data, serialized_str, len);
                    gst_memory_unmap(meta_mem, &map);
                    
                    /* Attach the memory block to the end of the buffer */
                    gst_buffer_append_memory(new_buf, meta_mem);
                    
                    GST_LOG_OBJECT(self, "Attached serialized metadata: %s", serialized_str);
                }
                g_free(serialized_str);
            }
        }
    }
    
    gst_caps_unref(thz_caps);

    /* 5. Clean up the old buffer and push the independent new buffer */
    gst_buffer_unref(buf);
    return gst_pad_push(self->srcpad, new_buf);
}

/* * GObject Initialization 
 */
static void thz_deep_copy_class_init(GstThzDeepCopyClass *klass) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_set_static_metadata(element_class,
        "TeraHertz Deep Copy & Serializer",
        "Filter/Metadata",
        "Deep copies H264 buffers and serializes custom THZ metadata for IPC survival",
        "Your Name <email@example.com>");

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
}

static void thz_deep_copy_init(GstThzDeepCopy *self) {
    self->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
    self->srcpad = gst_pad_new_from_static_template(&src_template, "src");

    /* Set the chain function */
    gst_pad_set_chain_function(self->sinkpad, thz_deep_copy_chain);

    /* Standard boilerplate to allow caps/allocation negotiation to pass through */
    GST_PAD_SET_PROXY_CAPS(self->sinkpad);
    GST_PAD_SET_PROXY_ALLOCATION(self->sinkpad);

    gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
    gst_element_add_pad(GST_ELEMENT(self), self->srcpad);
}

/* * Plugin Entry Point
 */
static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "thzdeepcopy", GST_RANK_NONE, GST_TYPE_THZ_DEEP_COPY);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    thzdeepcopy,
    "Deep copy and serialize metadata for IPC",
    plugin_init,
    "1.0",
    "Proprietary",
    "GStreamer",
    "https://example.com"
)
