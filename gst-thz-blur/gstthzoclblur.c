#define GST_USE_UNSTABLE_API 1
#ifndef PACKAGE
#define PACKAGE "thzoclblur"
#endif
#define VERSION "1.0.0"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/va/gstva.h>
#include <gst/allocators/gstdmabuf.h>

/* OpenCL Headers */
#include <CL/cl.h>
#include <CL/cl_ext.h>

#define KERNEL_PATH "/home/workspace/gst-plugins-thz/gst-thz-blur/blur.cl"

#define GST_TYPE_THZ_OCL_BLUR (gst_thz_ocl_blur_get_type())
G_DECLARE_FINAL_TYPE(GstThzOclBlur, gst_thz_ocl_blur, GST, THZ_OCL_BLUR, GstBaseTransform)

struct _GstThzOclBlur {
    GstBaseTransform parent;
    gint blur_strength;
    gint width, height;
    
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    cl_kernel kernel;
    gboolean initialized;
    
    GstVaDisplay *va_display; 
};

enum { PROP_0, PROP_BLUR_STRENGTH };
G_DEFINE_TYPE(GstThzOclBlur, gst_thz_ocl_blur, GST_TYPE_BASE_TRANSFORM);

static gboolean init_opencl(GstThzOclBlur *self) {
    cl_uint num_platforms;
    cl_platform_id platform = NULL;
    cl_device_id device = NULL;
    cl_int err;

    cl_platform_id platforms[10];
    if (clGetPlatformIDs(10, platforms, &num_platforms) != CL_SUCCESS) return FALSE;

    for (cl_uint i = 0; i < num_platforms; i++) {
        char name[1024];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(name), name, NULL);
        if (strstr(name, "Intel")) { platform = platforms[i]; break; }
    }
    if (!platform) return FALSE;

    if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL) != CL_SUCCESS) return FALSE;

    self->context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
    self->queue = clCreateCommandQueueWithProperties(self->context, device, NULL, &err);

    /* Load Kernel */
    FILE *f = fopen(KERNEL_PATH, "r");
    if (!f) {
        g_print("THZ-DEBUG: Failed to open kernel file at %s\n", KERNEL_PATH);
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *src = (char *)malloc(sz + 1);
    if (!src) { fclose(f); return FALSE; }
    fread(src, 1, sz, f);
    src[sz] = '\0';
    fclose(f);

    self->program = clCreateProgramWithSource(self->context, 1, (const char**)&src, NULL, &err);
    clBuildProgram(self->program, 1, &device, NULL, NULL, NULL);
    self->kernel = clCreateKernel(self->program, "blur_image", &err);
    free(src);

    self->initialized = TRUE;
    g_print("THZ-DEBUG: OpenCL Out-of-Place Initialized (RGBA). Device: Intel GPU\n");
    return TRUE;
}

static GstFlowReturn gst_thz_ocl_blur_transform(GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf) {
    GstThzOclBlur *self = GST_THZ_OCL_BLUR(trans);
    cl_int err;
    cl_mem cl_in_image = NULL;
    cl_mem cl_out_image = NULL;

    if (!self->initialized && !init_opencl(self)) return GST_FLOW_ERROR;

    /* Get DMA-BUF FDs for both buffers */
    GstMemory *in_mem = gst_buffer_peek_memory(inbuf, 0);
    GstMemory *out_mem = gst_buffer_peek_memory(outbuf, 0);

    if (!gst_is_dmabuf_memory(in_mem) || !gst_is_dmabuf_memory(out_mem)) {
        static gboolean warned_mem = FALSE;
        if (!warned_mem) {
            g_print("THZ-DEBUG: Input or Output memory is NOT DMA-BUF!\n");
            warned_mem = TRUE;
        }
        return GST_FLOW_ERROR;
    }

    int in_fd = gst_dmabuf_memory_get_fd(in_mem);
    int out_fd = gst_dmabuf_memory_get_fd(out_mem);

    if (in_fd < 0 || out_fd < 0) {
        g_print("THZ-DEBUG: Invalid FD (In: %d, Out: %d)\n", in_fd, out_fd);
        return GST_FLOW_ERROR;
    }

    /* Setup Image Formats and Descriptors */
    cl_image_format format = { CL_RGBA, CL_UNORM_INT8 };
    cl_image_desc desc = {0};
    desc.image_type = CL_MEM_OBJECT_IMAGE2D;
    desc.image_width = self->width;
    desc.image_height = self->height;

    /* Import Input Image */
    cl_mem_properties in_props[] = {
        CL_EXTERNAL_MEMORY_HANDLE_DMA_BUF_KHR, (cl_mem_properties)in_fd,
        0
    };
    cl_in_image = clCreateImageWithProperties(self->context, in_props, 
                                             CL_MEM_READ_ONLY, &format, &desc, NULL, &err);
    if (err != CL_SUCCESS) {
        g_print("THZ-DEBUG: clCreateImageWithProperties (Input) failed: %d\n", err);
        goto cleanup;
    }

    /* Import Output Image */
    cl_mem_properties out_props[] = {
        CL_EXTERNAL_MEMORY_HANDLE_DMA_BUF_KHR, (cl_mem_properties)out_fd,
        0
    };
    cl_out_image = clCreateImageWithProperties(self->context, out_props, 
                                              CL_MEM_WRITE_ONLY, &format, &desc, NULL, &err);
    if (err != CL_SUCCESS) {
        g_print("THZ-DEBUG: clCreateImageWithProperties (Output) failed: %d\n", err);
        goto cleanup;
    }

    /* Execute Kernel */
    clSetKernelArg(self->kernel, 0, sizeof(cl_mem), &cl_in_image);
    clSetKernelArg(self->kernel, 1, sizeof(cl_mem), &cl_out_image);
    clSetKernelArg(self->kernel, 2, sizeof(int), &self->blur_strength);
    
    size_t global[2] = { (size_t)self->width, (size_t)self->height };
    err = clEnqueueNDRangeKernel(self->queue, self->kernel, 2, NULL, global, NULL, 0, NULL, NULL);
    if (err != CL_SUCCESS) {
        g_print("THZ-DEBUG: clEnqueueNDRangeKernel failed: %d\n", err);
    }
    
    /* Ensure GPU work is done before releasing FDs back to GStreamer */
    clFinish(self->queue);

cleanup:
    if (cl_in_image) clReleaseMemObject(cl_in_image);
    if (cl_out_image) clReleaseMemObject(cl_out_image);

    if (err != CL_SUCCESS) return GST_FLOW_ERROR;

    return GST_FLOW_OK;
}

static void gst_thz_ocl_blur_set_context(GstElement *element, GstContext *context) {
    GstThzOclBlur *self = GST_THZ_OCL_BLUR(element);
    gst_va_handle_set_context(element, context, "gst.va.display", &self->va_display);
    GST_ELEMENT_CLASS(gst_thz_ocl_blur_parent_class)->set_context(element, context);
}

static gboolean gst_thz_ocl_blur_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    GstThzOclBlur *self = GST_THZ_OCL_BLUR(trans);
    GstVideoInfo info;
    if (gst_video_info_from_caps(&info, incaps)) {
        self->width = GST_VIDEO_INFO_WIDTH(&info);
        self->height = GST_VIDEO_INFO_HEIGHT(&info);
        g_print("THZ-DEBUG: Caps set. Resolution: %dx%d\n", self->width, self->height);
        return TRUE;
    }
    return FALSE;
}

static void gst_thz_ocl_blur_finalize(GObject *object) {
    GstThzOclBlur *self = GST_THZ_OCL_BLUR(object);
    g_print("THZ-DEBUG: Finalizing plugin and releasing OCL resources.\n");
    if (self->va_display) gst_object_unref(self->va_display);
    if (self->kernel) clReleaseKernel(self->kernel);
    if (self->program) clReleaseProgram(self->program);
    if (self->queue) clReleaseCommandQueue(self->queue);
    if (self->context) clReleaseContext(self->context);
    G_OBJECT_CLASS(gst_thz_ocl_blur_parent_class)->finalize(object);
}

static void gst_thz_ocl_blur_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstThzOclBlur *self = GST_THZ_OCL_BLUR(object);
    if (prop_id == PROP_BLUR_STRENGTH) self->blur_strength = g_value_get_int(value);
}

static void gst_thz_ocl_blur_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstThzOclBlur *self = GST_THZ_OCL_BLUR(object);
    if (prop_id == PROP_BLUR_STRENGTH) g_value_set_int(value, self->blur_strength);
}

static void gst_thz_ocl_blur_class_init(GstThzOclBlurClass *klass) {
    GObjectClass *g_class = G_OBJECT_CLASS(klass);
    GstElementClass *e_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *b_class = GST_BASE_TRANSFORM_CLASS(klass);

    g_class->set_property = gst_thz_ocl_blur_set_property;
    g_class->get_property = gst_thz_ocl_blur_get_property;
    g_class->finalize = gst_thz_ocl_blur_finalize;
    e_class->set_context = GST_DEBUG_FUNCPTR(gst_thz_ocl_blur_set_context);
    
    b_class->transform = GST_DEBUG_FUNCPTR(gst_thz_ocl_blur_transform);
    b_class->set_caps = GST_DEBUG_FUNCPTR(gst_thz_ocl_blur_set_caps);

    g_object_class_install_property(g_class, PROP_BLUR_STRENGTH, 
        g_param_spec_int("blur-strength", "Strength", "0-100", 0, 100, 50, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(e_class, "THZ OCL RGBA Blur", "Filter", "DMA-BUF Zero-Copy Transform", "Gemini");

    GstStaticPadTemplate sink_t = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, 
        GST_STATIC_CAPS("video/x-raw(memory:DMABuf), format=RGBA"));
    GstStaticPadTemplate src_t = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, 
        GST_STATIC_CAPS("video/x-raw(memory:DMABuf), format=RGBA"));
    
    gst_element_class_add_pad_template(e_class, gst_static_pad_template_get(&sink_t));
    gst_element_class_add_pad_template(e_class, gst_static_pad_template_get(&src_t));
}

static void gst_thz_ocl_blur_init(GstThzOclBlur *self) { 
    self->blur_strength = 50; 
    self->initialized = FALSE; 
    self->va_display = NULL;
}

static gboolean plugin_init(GstPlugin *plugin) { 
    return gst_element_register(plugin, "thzoclblur", GST_RANK_NONE, GST_TYPE_THZ_OCL_BLUR); 
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, thzoclblur, "OCL RGBA Zero-Copy", plugin_init, VERSION, "LGPL", "GStreamer", "https://gstreamer.net/")