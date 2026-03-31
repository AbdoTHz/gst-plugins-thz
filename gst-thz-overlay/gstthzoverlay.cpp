#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <opencv2/opencv.hpp>
#include <cmath>
#include <vector>
#include <string>

#ifndef PACKAGE
#define PACKAGE "thzoverlay"
#endif

#define ICON_SIZE 200
#define DEFAULT_TIMEOUT_MS 3000

// Property IDs
enum {
  PROP_0,
  PROP_TIMEOUT
};

#define GST_TYPE_THZ_OVERLAY (gst_thz_overlay_get_type())
G_DECLARE_FINAL_TYPE (GstThzOverlay, gst_thz_overlay, GST, THZ_OVERLAY, GstVideoFilter)

struct ThreatState {
    bool active = false;
    bool is_all_clear = false; // Flag to distinguish status from specific threats
    GstClockTime start_time = GST_CLOCK_TIME_NONE;
    std::string type;
    double confidence = 0.0;
    int x = 0, y = 0, w = 0, h = 0;
};

struct _GstThzOverlay {
  GstVideoFilter parent;
  uint32_t frame_count;
  
  // Property Storage
  guint timeout_ms;

  // Icon Assets
  cv::Mat gun_icon_alpha;        
  cv::Mat gun_icon_channels[3];
  cv::Mat knife_icon_alpha;        
  cv::Mat knife_icon_channels[3];  
  bool gun_icon_loaded;
  bool knife_icon_loaded;

  // Persistence State
  ThreatState current_threat;
};

G_DEFINE_TYPE (GstThzOverlay, gst_thz_overlay, GST_TYPE_VIDEO_FILTER);

static void
gst_thz_overlay_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstThzOverlay *self = GST_THZ_OVERLAY (object);
  switch (prop_id) {
    case PROP_TIMEOUT:
      self->timeout_ms = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_thz_overlay_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstThzOverlay *self = GST_THZ_OVERLAY (object);
  switch (prop_id) {
    case PROP_TIMEOUT:
      g_value_set_uint (value, self->timeout_ms);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_thz_overlay_transform_frame_ip (GstVideoFilter * filter, GstVideoFrame * frame)
{
  GstThzOverlay *cp = GST_THZ_OVERLAY (filter);
  GstBuffer *buf = frame->buffer;
  
  GstClockTime current_time = GST_BUFFER_PTS(buf);
  if (!GST_CLOCK_TIME_IS_VALID(current_time)) {
      current_time = cp->frame_count * (GST_SECOND / 30); 
  }

  int width = GST_VIDEO_FRAME_WIDTH(frame);
  int height = GST_VIDEO_FRAME_HEIGHT(frame);
  int stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
  cv::Mat img(height, width, CV_8UC4, GST_VIDEO_FRAME_PLANE_DATA(frame, 0), stride);

  // 1. Define the Square Subframe Area
  auto get_env_int = [](const char* name) {
      const char* val = std::getenv(name);
      try {
          return val ? std::stoi(val) : 0; 
      } catch (...) {
          return 0;
      }
  };

  int sub_x  = get_env_int("SUB_X");
  int sub_y  = get_env_int("SUB_Y");
  int side_x = get_env_int("SIDE_X");
  int side_y = get_env_int("SIDE_Y");
  cv::Rect subframe_rect(sub_x, sub_y, side_x, side_y);

  // 2. Visual Dimming Logic
  img *= 0.8; 
  cv::Mat subframe_view = img(subframe_rect);
  subframe_view *= 1.25; 

  // Metadata Extraction
  GstReferenceTimestampMeta *meta = gst_buffer_get_reference_timestamp_meta(buf, NULL);

  if (meta) {
    GstStructure *s = (GstStructure *)meta->timestamp;
    gboolean threat_detected = FALSE;
    gboolean person_detected = FALSE;
    
    gst_structure_get_boolean(s, "threat_detected", &threat_detected);
    gst_structure_get_boolean(s, "person_detected", &person_detected);
    printf("Overlay Metadata: - Threat Detected: %d, Person Detected: %d\n", threat_detected, person_detected);
    if (threat_detected) {
        const gchar* t_type = gst_structure_get_string(s, "threat_type");
        cp->current_threat.type = (t_type != NULL) ? t_type : "Unknown";
        cp->current_threat.active = true;
        cp->current_threat.is_all_clear = false;
        cp->current_threat.start_time = current_time;

        const GValue *prob_val = gst_structure_get_value(s, "probabilities");
        float max_prob = 0.0f;
        int max_index = 0;
        if (prob_val && GST_VALUE_HOLDS_ARRAY(prob_val)) {
            guint n = gst_value_array_get_size(prob_val);
            for (guint i = 0; i < n; i++) {
                const GValue *v = gst_value_array_get_value(prob_val, i);
                if (G_VALUE_HOLDS_FLOAT(v)) {
                    float current_p = g_value_get_float(v);
                    if (current_p > max_prob) {
                        max_index = i;
                        max_prob = current_p;
                    }
                }
            }
        }
        cp->current_threat.confidence = max_prob;

        gchar roi_key[8];
        g_snprintf(roi_key, sizeof(roi_key), "roi%d", max_index + 1);
        const GValue *roi_val = gst_structure_get_value(s, roi_key);
        
        if (roi_val && GST_VALUE_HOLDS_ARRAY(roi_val)) {
            float x1 = g_value_get_float(gst_value_array_get_value(roi_val, 0));
            float y1 = g_value_get_float(gst_value_array_get_value(roi_val, 1));
            float x2 = g_value_get_float(gst_value_array_get_value(roi_val, 3));
            float y2 = g_value_get_float(gst_value_array_get_value(roi_val, 4));

            cp->current_threat.x = sub_x + (int)(x1 * side_x);
            cp->current_threat.y = sub_y + (int)(y1 * side_y);
            cp->current_threat.w = (int)((x2 - x1) * side_x);
            cp->current_threat.h = (int)((y2 - y1) * side_y);
        }
    } else if (person_detected) {
        // Trigger ALL CLEAR if a person is there but no weapon found
        cp->current_threat.active = true;
        cp->current_threat.is_all_clear = true;
        cp->current_threat.type = "ALL CLEAR";
        cp->current_threat.start_time = current_time;
    }
  }

  // 3. Persistent Overlay Rendering
  if (cp->current_threat.active) {
    if (current_time > cp->current_threat.start_time + (cp->timeout_ms * GST_MSECOND)) {
        cp->current_threat.active = false;
        cp->current_threat.is_all_clear = false;
    } else {
        if (cp->current_threat.is_all_clear) {
            // --- ALL CLEAR RENDERING (CENTERED) ---
            std::string text = "ALL CLEAR";
            int font_face = cv::FONT_HERSHEY_SIMPLEX;
            double font_scale = 2.5;
            int thickness = 4;
            int baseline = 0;
            cv::Size text_size = cv::getTextSize(text, font_face, font_scale, thickness, &baseline);
            
            cv::Point text_org((width - text_size.width) / 2, (height + text_size.height) / 2);
            
            // Draw green background for "Safe" status
            cv::Rect bg_rect(text_org.x - 20, text_org.y - text_size.height - 20, text_size.width + 40, text_size.height + 40);
            cv::rectangle(img, bg_rect, cv::Scalar(0, 180, 0, 200), cv::FILLED);
            cv::putText(img, text, text_org, font_face, font_scale, cv::Scalar(255, 255, 255, 255), thickness);
            
        } else {
            // --- THREAT RENDERING (GUN/KNIFE) ---
            int rx = std::max(0, cp->current_threat.x);
            int ry = std::max(0, cp->current_threat.y);
            int rw = std::min(cp->current_threat.w, width - rx);
            int rh = std::min(cp->current_threat.h, height - ry);

            if (rw > 0 && rh > 0) {
                cv::rectangle(img, subframe_rect, cv::Scalar(255, 255, 255, 100), 1);

                cv::Mat* src_chans = (cp->current_threat.type == "Knife") ? cp->knife_icon_channels : cp->gun_icon_channels;
                cv::Mat* src_a = (cp->current_threat.type == "knife") ? &cp->knife_icon_alpha : &cp->gun_icon_alpha ;
                bool loaded = (cp->current_threat.type == "Knife") ?  cp->knife_icon_loaded : cp->gun_icon_loaded ;

                if (loaded) {
                    cv::Rect roi_rect(rx, ry, rw, rh);
                    cv::Mat roi = img(roi_rect);
                    cv::Size sz(rw, rh);
                    cv::Mat res_alpha;
                    cv::resize(*src_a, res_alpha, sz);

                    for (int i = 0; i < 3; i++) {
                        cv::Mat fg_resized, bg_chan;
                        cv::resize(src_chans[i], fg_resized, sz);
                        cv::extractChannel(roi, bg_chan, i);
                        bg_chan.convertTo(bg_chan, CV_32F);
                        cv::Mat blended;
                        cv::multiply(fg_resized, res_alpha, fg_resized);
                        cv::multiply(bg_chan, 1.0 - res_alpha, bg_chan);
                        cv::add(fg_resized, bg_chan, blended);
                        blended.convertTo(blended, CV_8U);
                        cv::insertChannel(blended, roi, i);
                    }
                }

                cv::Scalar red(255, 0, 0, 255);
                cv::rectangle(img, cv::Rect(rx, ry, rw, rh), red, 5);
                std::string label = cp->current_threat.type + " " + std::to_string((int)(cp->current_threat.confidence * 100)) + "%";
                cv::putText(img, label, cv::Point(rx, ry - 5), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255,255,255,255), 1);
            }
        }
    }
  }

  cp->frame_count++;
  return GST_FLOW_OK;
}

static void gst_thz_overlay_init (GstThzOverlay * cp) {
    cp->frame_count = 0;
    cp->timeout_ms = DEFAULT_TIMEOUT_MS;
    cp->gun_icon_loaded = false;
    cp->knife_icon_loaded = false;
    cp->current_threat.active = false;
    cp->current_threat.is_all_clear = false;

    auto load_icon = [](std::string path, bool &loaded, cv::Mat* channels, cv::Mat& alpha) {
        cv::Mat raw = cv::imread(path, cv::IMREAD_UNCHANGED);
        if (!raw.empty() && raw.channels() >= 4) {
            cv::Mat resized;
            cv::resize(raw, resized, cv::Size(ICON_SIZE, ICON_SIZE));
            cv::Mat rgba[4];
            cv::split(resized, rgba);
            rgba[3].convertTo(alpha, CV_32FC1, 1.0/255.0);
            rgba[2].convertTo(channels[0], CV_32FC1);
            rgba[1].convertTo(channels[1], CV_32FC1);
            rgba[0].convertTo(channels[2], CV_32FC1);
            loaded = true;
        }
    };
    load_icon("/home/ubuntu/images/gun.webp", cp->gun_icon_loaded, cp->gun_icon_channels, cp->gun_icon_alpha);
    load_icon("/home/ubuntu/images/knife.png", cp->knife_icon_loaded, cp->knife_icon_channels, cp->knife_icon_alpha);
}

static void
gst_thz_overlay_class_init (GstThzOverlayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_thz_overlay_set_property;
  gobject_class->get_property = gst_thz_overlay_get_property;

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "Timeout", "Timeout for overlay in milliseconds",
          0, G_MAXUINT, DEFAULT_TIMEOUT_MS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_metadata (element_class, "THZ Threat Overlay", "Filter/Effect",
      "Persistent icon overlay with configurable timeout", "Developer");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_from_string ("video/x-raw, format=RGBA")));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_caps_from_string ("video/x-raw, format=RGBA")));

  video_filter_class->transform_frame_ip = GST_DEBUG_FUNCPTR (gst_thz_overlay_transform_frame_ip);
}

static gboolean plugin_init (GstPlugin * plugin) {
  return gst_element_register (plugin, "thzoverlay", GST_RANK_NONE, GST_TYPE_THZ_OVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, thzoverlay,
    "THZ Threat Overlay Plugin", plugin_init, "1.0", "LGPL", "GStreamer", "https://example.com/")