cmake_minimum_required(VERSION 3.10)
project(gst-thz-silhouette VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DGST_USE_UNSTABLE_API)

find_package(PkgConfig REQUIRED)

# Standard GStreamer and OpenCV modules
pkg_check_modules(GST REQUIRED 
    gstreamer-1.0 
    gstreamer-base-1.0
    gstreamer-video-1.0 
    gstreamer-va-1.0
)
pkg_check_modules(OPENCV REQUIRED opencv4)

# Intel DL Streamer specific paths
include_directories(/opt/intel/dlstreamer/include)
include_directories(/opt/intel/dlstreamer/gstreamer/include/gstreamer-1.0)

# Library search paths
link_directories(/opt/intel/dlstreamer/gstreamer/lib)
link_directories(/opt/intel/dlstreamer/lib)

# Using MODULE is preferred for GStreamer plugins (dynamically loaded at runtime)
add_library(gstthzsilhouette MODULE gstthzsilhouette.cpp)

# CRITICAL: This pulls in glib.h and all GStreamer dependencies
target_include_directories(gstthzsilhouette PRIVATE 
    ${GST_INCLUDE_DIRS} 
    ${OPENCV_INCLUDE_DIRS}
)

target_link_libraries(gstthzsilhouette PRIVATE 
    ${GST_LIBRARIES} 
    ${OPENCV_LIBRARIES}
    gstanalytics-1.0
    gstvideoanalytics
    dlstreamer_gst_meta
)

# Ensure the output file name follows GStreamer plugin naming conventions (libgstthzsilhouette.so)
set_target_properties(gstthzsilhouette PROPERTIES 
    PREFIX "lib" 
    OUTPUT_NAME "gstthzsilhouette"
)

# --- Installation Logic ---

# Get the GStreamer plugin installation directory via pkg-config
execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pluginsdir gstreamer-1.0
                OUTPUT_VARIABLE GST_PLUGINS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT GST_PLUGINS_DIR)
    # Fallback path if pkg-config fails (common for Debian/Ubuntu)
    set(GST_PLUGINS_DIR "/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
endif()

message(STATUS "Silhouette plugin will be installed to: ${GST_PLUGINS_DIR}")

# Install the library to the detected GStreamer path
install(TARGETS gstthzsilhouette DESTINATION ${GST_PLUGINS_DIR})