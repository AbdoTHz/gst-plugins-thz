cmake_minimum_required(VERSION 3.10)
project(terahertz-overlay-plugin VERSION 1.0)

# C++17 is recommended for modern GStreamer/OpenCV integrations
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Required definitions for GStreamer plugin names
add_definitions(-DPACKAGE="terahertz-overlay")

find_package(PkgConfig REQUIRED)

# 1. Find GStreamer dependencies
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)

# 2. Find OpenCV (specifically for the overlay functionality)
find_package(OpenCV REQUIRED)

# 3. Define Target: The Metadata Consumer (thzoverlay)
# Using MODULE is standard for plugins, but SHARED works as well
add_library(gstthzoverlay SHARED gstthzoverlay.cpp)

target_include_directories(gstthzoverlay PRIVATE 
    ${GST_INCLUDE_DIRS} 
    ${GST_VIDEO_INCLUDE_DIRS} 
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(gstthzoverlay PRIVATE 
    ${GST_LIBRARIES} 
    ${GST_VIDEO_LIBRARIES} 
    ${OpenCV_LIBS}
)

# Ensure the output is named libgstthzoverlay.so
set_target_properties(gstthzoverlay PROPERTIES 
    PREFIX "lib"
    OUTPUT_NAME "gstthzoverlay"
)

# --- Installation Logic ---

# Get the GStreamer plugin installation directory via pkg-config
execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pluginsdir gstreamer-1.0
                OUTPUT_VARIABLE GST_PLUGINS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT GST_PLUGINS_DIR)
    # Fallback path if pkg-config fails
    set(GST_PLUGINS_DIR "/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
endif()

message(STATUS "Overlay plugin will be installed to: ${GST_PLUGINS_DIR}")

install(TARGETS gstthzoverlay DESTINATION ${GST_PLUGINS_DIR})
