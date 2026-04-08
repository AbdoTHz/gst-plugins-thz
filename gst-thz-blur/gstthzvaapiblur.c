#define GST_USE_UNSTABLE_API 1
#ifndef PACKAGE
#define PACKAGE "thzvaapiblur"
#endif
#define VERSION "1.0.0"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>
/* Modern GstVA Headers */
#include <gst/va/gstva.h>

#include <CL/cl.h>
#include <CL/cl_va_api_media_sharing_intel.h>

/* Path to your OpenCL kernel file */
#define KERNEL_PATH "../blur.cl"

#define GST_TYPE_THZ_VAAPI_BLUR (gst_thz_vaapi_blur_get_type())
G_DECLARE_FINAL_TYPE(GstThzVaapiBlur, gst_thz_vaapi_blur, GST, THZ_VAAPI_BLUR, GstBaseTransform)

struct _GstThzVaapiBlur {
    GstBaseTransform parent;
    gint blur_strength;
    gint width, height;
    
    /* OpenCL Handles */
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    gboolean initialized;

    /* Intel OpenCL Extensions */
    clCreateFromVA_APIMediaSurfaceINTEL_fn clCreateFromVA_APIMediaSurfaceINTEL;
    clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn clEnqueueAcquireVA_APIMediaSurfacesINTEL;
    clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn clEnqueueReleaseVA_APIMediaSurfacesINTEL;
    
    GstVaDisplay *va_display; 
};

enum { PROP_0, PROP_BLUR_STRENGTH };
G_DEFINE_TYPE(GstThzVaapiBlur, gst_thz_vaapi_blur, GST_TYPE_BASE_TRANSFORM);

static gboolean init_opencl(GstThzVaapiBlur *self) {
    cl_uint num_platforms;
    cl_platform_id platform;
    cl_int err;

    g_print("THZ-DEBUG: Initializing OpenCL...\n");

    if (clGetPlatformIDs(1, &platform, &num_platforms) != CL_SUCCESS) return FALSE;
    cl_device_id device;
    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL) != CL_SUCCESS) return FALSE;

    self->context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    self->queue = clCreateCommandQueueWithProperties(self->context, device, NULL, &err);

    /* Load Kernel */
    FILE *f = fopen(KERNEL_PATH, "r");
    if (!f) {
        g_print("THZ-DEBUG: Could not open kernel file at %s\n", KERNEL_PATH);
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *src = (char *)malloc(sz + 1);
    size_t rbytes = fread(src, 1, sz, f);
    src[rbytes] = '\0';
    fclose(f);

    self->program = clCreateProgramWithSource(self->context, 1, (const char**)&src, NULL, &err);
    err = clBuildProgram(self->program, 1, &device, NULL, NULL, NULL);
    if (err != CL_SUCCESS) {
        char buffer[2048];
        clGetProgramBuildInfo(self->program, device, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, NULL);
        g_print("THZ-DEBUG: CL Build Error: %s\n", buffer);
        free(src);
        return FALSE;
    }
    self->kernel = clCreateKernel(self->program, "blur_image", &err);
    free(src);

    /* Get Intel Extensions */
    self->clCreateFromVA_APIMediaSurfaceINTEL = (clCreateFromVA_APIMediaSurfaceINTEL_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clCreateFromVA_APIMediaSurfaceINTEL");
    self->clEnqueueAcquireVA_APIMediaSurfacesINTEL = (clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireVA_APIMediaSurfacesINTEL");
    self->clEnqueueReleaseVA_APIMediaSurfacesINTEL = (clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn)
        clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseVA_APIMediaSurfacesINTEL");

    if (!self->clCreateFromVA_APIMediaSurfaceINTEL) {
        g_print("THZ-DEBUG: Intel VA-API Media Sharing extensions NOT found!\n");
        return FALSE;
    }

    self->initialized = TRUE;
    g_print("THZ-DEBUG: OpenCL successfully initialized.\n");
    return TRUE;
}

static void gst_thz_vaapi_blur_set_context(GstElement *element, GstContext *context) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(element);
    /* Attempt to share the VA display context between elements */
    gst_va_handle_set_context(element, context, "gst.va.display", &self->va_display);
    GST_ELEMENT_CLASS(gst_thz_vaapi_blur_parent_class)->set_context(element, context);
}

static GstFlowReturn gst_thz_vaapi_blur_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(trans);
    if (!self->initialized && !init_opencl(self)) return GST_FLOW_ERROR;

    VASurfaceID surface = VA_INVALID_ID;

    /* 1. Extract Surface using Modern GstVA API */
    surface = gst_va_buffer_get_surface(buf);

    if (surface != VA_INVALID_ID) {
        g_print("THZ-DEBUG: Success! Surface %u obtained via gst_va_buffer_get_surface\n", surface);
    } else {
        g_print("THZ-DEBUG: gst_va_buffer_get_surface failed. Attempting fallback...\n");

        /* Fallback: Peek memory allocator */
        GstMemory *mem = gst_buffer_peek_memory(buf, 0);
        if (mem) {
            const gchar *allocator_name = (mem->allocator && mem->allocator->mem_type) ? 
                                          mem->allocator->mem_type : "unknown";
            g_print("THZ-DEBUG: Memory detected. Allocator type: %s\n", allocator_name);

            {
                surface = gst_va_memory_get_surface(mem);
                if (surface != VA_INVALID_ID) {
                    g_print("THZ-DEBUG: Success! Surface %u obtained via fallback (va_memory_get_surface)\n", surface);
                } else {
                    g_print("THZ-DEBUG: Fallback failed. Memory is VA, but Surface ID is invalid.\n");
                }
            }
            g_print("THZ-DEBUG: Fallback failed. No memory found in buffer.\n");
        }
    }
    if (surface == VA_INVALID_ID) {
        g_print("THZ-DEBUG: ERROR - Failed to extract VASurfaceID from buffer.\n");
        return GST_FLOW_OK;
    }

    /* 2. Map VA-API Surface to OpenCL Image */
    cl_int err;
    cl_mem cl_image = self->clCreateFromVA_APIMediaSurfaceINTEL(
        self->context, 
        CL_MEM_READ_WRITE, 
        &surface, 
        0, // Plane index (0 for BGRA)
        &err
    );
    
    if (err == CL_SUCCESS && cl_image) {
        /* Lock surface for GPU compute */
        self->clEnqueueAcquireVA_APIMediaSurfacesINTEL(self->queue, 1, &cl_image, 0, NULL, NULL);
        
        clSetKernelArg(self->kernel, 0, sizeof(cl_mem), &cl_image);
        clSetKernelArg(self->kernel, 1, sizeof(cl_mem), &cl_image);
        clSetKernelArg(self->kernel, 2, sizeof(int), &self->blur_strength);
        
        size_t global[2] = { (size_t)self->width, (size_t)self->height };
        cl_int k_err = clEnqueueNDRangeKernel(self->queue, self->kernel, 2, NULL, global, NULL, 0, NULL, NULL);
        
        if (k_err != CL_SUCCESS) {
            g_print("THZ-DEBUG: Kernel Execution Error: %d\n", k_err);
        }

        /* Unlock and release */
        self->clEnqueueReleaseVA_APIMediaSurfacesINTEL(self->queue, 1, &cl_image, 0, NULL, NULL);
        clFinish(self->queue);
        clReleaseMemObject(cl_image);
    } else {
        g_print("THZ-DEBUG: OpenCL Mapping FAILED with error: %d for surface %u\n", err, surface);
    }

    return GST_FLOW_OK;
}

static gboolean gst_thz_vaapi_blur_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(trans);
    GstVideoInfo info;
    if (gst_video_info_from_caps(&info, incaps)) {
        self->width = info.width;
        self->height = info.height;
        g_print("THZ-DEBUG: Caps set. Res: %dx%d\n", self->width, self->height);
        return TRUE;
    }
    return FALSE;
}

static void gst_thz_vaapi_blur_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(object);
    if (prop_id == PROP_BLUR_STRENGTH) self->blur_strength = g_value_get_int(value);
}

static void gst_thz_vaapi_blur_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstThzVaapiBlur *self = GST_THZ_VAAPI_BLUR(object);
    if (prop_id == PROP_BLUR_STRENGTH) g_value_set_int(value, self->blur_strength);
}

static void gst_thz_vaapi_blur_class_init(GstThzVaapiBlurClass *klass) {
    GObjectClass *g_class = G_OBJECT_CLASS(klass);
    GstElementClass *e_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *b_class = GST_BASE_TRANSFORM_CLASS(klass);

    g_class->set_property = gst_thz_vaapi_blur_set_property;
    g_class->get_property = gst_thz_vaapi_blur_get_property;
    e_class->set_context = GST_DEBUG_FUNCPTR(gst_thz_vaapi_blur_set_context);
    b_class->transform_ip = GST_DEBUG_FUNCPTR(gst_thz_vaapi_blur_transform_ip);
    b_class->set_caps = GST_DEBUG_FUNCPTR(gst_thz_vaapi_blur_set_caps);

    g_object_class_install_property(g_class, PROP_BLUR_STRENGTH, 
        g_param_spec_int("blur-strength", "Strength", "0-100", 0, 100, 50, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(e_class, "THZ VAAPI BGRA Blur", "Filter", "Intel GPU BGRA Zero-Copy", "Gemini");

    /* Ensure we negotiate for VASurface memory */
    GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, 
        GST_STATIC_CAPS("video/x-raw(memory:VASurface), format=BGRA"));
    GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, 
        GST_STATIC_CAPS("video/x-raw(memory:VASurface), format=BGRA"));
    
    gst_element_class_add_pad_template(e_class, gst_static_pad_template_get(&sink_t));
    gst_element_class_add_pad_template(e_class, gst_static_pad_template_get(&src_t));
}

static void gst_thz_vaapi_blur_init(GstThzVaapiBlur *self) { self->blur_strength = 50; self->initialized = FALSE; }
static gboolean plugin_init(GstPlugin *plugin) { return gst_element_register(plugin, "thzvaapiblur", GST_RANK_NONE, GST_TYPE_THZ_VAAPI_BLUR); }
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, thzvaapiblur, "VAAPI BGRA Zero-Copy", plugin_init, VERSION, "LGPL", "GStreamer", "https://gstreamer.net/")