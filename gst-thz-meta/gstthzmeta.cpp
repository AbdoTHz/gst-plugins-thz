#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <time.h>
#include <pthread.h>
#include <sdbus-c++/sdbus-c++.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

using json = nlohmann::json;

/* --- GStreamer Plugin Definitions --- */
#define GST_TYPE_META_PLUGIN (gst_thz_meta_get_type())
G_DECLARE_FINAL_TYPE(GstTHZMeta, gst_thz_meta, GST, META_PLUGIN, GstBaseTransform)

struct _GstTHZMeta {
    GstBaseTransform parent;
    guint64 internal_frame_count;
    pthread_t dbus_thread;
    GAsyncQueue *metadata_queue;
};

G_DEFINE_TYPE(GstTHZMeta, gst_thz_meta, GST_TYPE_BASE_TRANSFORM);

/* --- DBus Background Logic --- */
void onNewDetection(sdbus::Signal& signal, gpointer user_data) {
    GstTHZMeta *self = GST_META_PLUGIN(user_data);
    try {
        std::string jsonPayload;
        signal >> jsonPayload;
        g_async_queue_push(self->metadata_queue, g_strdup(jsonPayload.c_str()));
    } catch (const std::exception& e) {
        std::cerr << "[TRACE] DBus Signal Error: " << e.what() << std::endl;
    }
}

static void* dbus_worker_thread(void* data) {
    GstTHZMeta *self = GST_META_PLUGIN(data);
    try {
        auto connection = sdbus::createSessionBusConnection();
        // This works in both sdbus-c++ v1.x and v2.x
        auto proxy = sdbus::createProxy(*connection, 
                                sdbus::ServiceName{"com.embedded.DetectionSystem"}, 
                                sdbus::ObjectPath{"/com/embedded/DetectionSystem"});
        proxy->registerSignalHandler("com.embedded.DetectionSystem.Signals", "NewDetection", 
            [self](sdbus::Signal& sig){ onNewDetection(sig, self); });
        proxy->finishRegistration();
        std::cout << "[TRACE] DBus Worker Thread Started & Registered" << std::endl;
        connection->enterEventLoop(); 
    } catch (const std::exception& e) {
        std::cerr << "[TRACE] DBus Thread Failed: " << e.what() << std::endl;
    }
    return NULL;
}

static void metadata_destroyed_notify(gpointer data) {
    gst_structure_free((GstStructure *)data);
}

static GstFlowReturn gst_thz_meta_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstTHZMeta *self = GST_META_PLUGIN(trans);
    
    // Increment the camera frame counter for every buffer passing through
    guint64 current_camera_frame = self->internal_frame_count++;

    // If no DBus metadata is available, we just pass through
    if (g_async_queue_length(self->metadata_queue) <= 0) return GST_FLOW_OK;

    gpointer raw_json_ptr;
    while ((raw_json_ptr = g_async_queue_try_pop(self->metadata_queue)) != NULL) {
        gchar *json_str = (gchar *)raw_json_ptr;
        
        if (json_str != nullptr) {
            std::cout << "[DBus Sync] Frame #" << current_camera_frame 
                      << " Received JSON: " << json_str << std::endl;
        }

        try {
            json data = json::parse(json_str);

            std::string type_val   = data.value("type", "unknown");
            std::string scan_id    = data.value("scan_id", "N/A");
            std::string threat_t   = data.value("threat_type", "None");
            std::string scan_msg   = data.value("scan_message", "");
            
            gboolean threat_bool = data.value("threat_detected", false) ? TRUE : FALSE;
            gboolean person_bool = data.value("person_detected", false) ? TRUE : FALSE;

            // Create the structure and include the local 'camera_frame'
            GstStructure *s = gst_structure_new("TeraHertzMeta",
                "type",            G_TYPE_STRING,  type_val.c_str(),
                "scan_id",         G_TYPE_STRING,  scan_id.c_str(),
                "timestamp",       G_TYPE_UINT64,  (guint64)data.value("timestamp", 0),
                "camera_frame",    G_TYPE_UINT64,  current_camera_frame,
                "thz_frame",       G_TYPE_UINT,    (guint)data.value("thz_frame", 0), 
                "threat_detected", G_TYPE_BOOLEAN, threat_bool,
                "person_detected", G_TYPE_BOOLEAN, person_bool,
                "threat_type",     G_TYPE_STRING,  threat_t.c_str(),
                "scan_message",    G_TYPE_STRING,  scan_msg.c_str(),
                NULL);
                
            auto add_float_array = [&](const std::string& key) {
                if (data.contains(key) && data[key].is_array()) {
                    GValue array_val = G_VALUE_INIT;
                    g_value_init(&array_val, GST_TYPE_ARRAY);
                    
                    for (auto& element : data[key]) {
                        if (!element.is_number()) continue;
                        GValue elem = G_VALUE_INIT;
                        g_value_init(&elem, G_TYPE_FLOAT);
                        g_value_set_float(&elem, element.get<float>());
                        gst_value_array_append_value(&array_val, &elem);
                        g_value_unset(&elem);
                    }
                    gst_structure_take_value(s, key.c_str(), &array_val);
                }
            };

            add_float_array("probabilities");
            for (int i = 1; i <= 6; ++i) {
                add_float_array("roi" + std::to_string(i));
            }

            // Attach structure to buffer as Reference Timestamp Meta
            GstCaps *dummy_caps = gst_caps_from_string("thz");
            gst_buffer_add_reference_timestamp_meta(buf, dummy_caps, (GstClockTime)s, GST_CLOCK_TIME_NONE);
            gst_caps_unref(dummy_caps);
            
            // Set cleanup notification so the structure is freed when the buffer is destroyed
            static GQuark cleanup_quark = g_quark_from_static_string("cleanup_notify");
            gst_mini_object_set_qdata(GST_MINI_OBJECT(buf), cleanup_quark, s, (GDestroyNotify)gst_structure_free);

        } catch (const std::exception& e) {
            GST_ERROR_OBJECT(self, "Exception during transform: %s", e.what());
        }
        g_free(json_str);
    }
    return GST_FLOW_OK;
}

/* --- Plugin Boilerplate --- */
static void gst_thz_meta_finalize(GObject *object) {
    GstTHZMeta *self = GST_META_PLUGIN(object);
    if (self->metadata_queue) g_async_queue_unref(self->metadata_queue);
    G_OBJECT_CLASS(gst_thz_meta_parent_class)->finalize(object);
}

static void gst_thz_meta_init(GstTHZMeta *self) {
    self->internal_frame_count = 0;
    self->metadata_queue = g_async_queue_new_full((GDestroyNotify)g_free);
    pthread_create(&self->dbus_thread, NULL, dbus_worker_thread, self);
}

static void gst_thz_meta_class_init(GstTHZMetaClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->finalize = gst_thz_meta_finalize;
    gst_element_class_set_static_metadata(element_class, "TeraHertz THZMeta", "Filter/Metadata", "Metadata trace plugin", "Developer");

    GstCaps *caps = gst_caps_from_string("video/x-raw");
    gst_element_class_add_pad_template(element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);

    base_class->transform_ip = GST_DEBUG_FUNCPTR(gst_thz_meta_transform_ip);
}

static gboolean plugin_init(GstPlugin *plugin) {
    return gst_element_register(plugin, "thzmeta", GST_RANK_NONE, GST_TYPE_META_PLUGIN);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, thzmeta, "TeraHertz Metadata", plugin_init, "1.0", "LGPL", "GStreamer", "https://gstreamer.net/")
