cmake_minimum_required(VERSION 3.10)
project(gst-thzvaapiblur C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST_VA REQUIRED gstreamer-va-1.0)

include_directories(${GST_VA_INCLUDE_DIRS})

# Remove gstreamer-vaapi-1.0 here
pkg_check_modules(GST REQUIRED 
    gstreamer-1.0 
    gstreamer-video-1.0 
    gstreamer-allocators-1.0 
    gstreamer-va-1.0
    gmodule-2.0
)

find_package(OpenCL REQUIRED)

add_library(gstthzvaapiblur SHARED gstthzvaapiblur.c)

target_compile_definitions(gstthzvaapiblur PRIVATE 
    CL_TARGET_OPENCL_VERSION=300
    KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/blur.cl"
)

target_include_directories(gstthzvaapiblur PRIVATE ${GST_INCLUDE_DIRS} ${OpenCL_INCLUDE_DIRS})
target_link_libraries(gstthzvaapiblur ${GST_LIBRARIES} ${OpenCL_LIBRARIES} ${GST_VA_LIBRARIES})