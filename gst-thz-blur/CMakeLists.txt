cmake_minimum_required(VERSION 3.10)
project(gst-thzoclblur C)

# Use standard C99 or higher for GStreamer
set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)

# Combine all GStreamer dependencies
pkg_check_modules(GST REQUIRED 
    gstreamer-1.0 
    gstreamer-base-1.0
    gstreamer-video-1.0 
    gstreamer-allocators-1.0 
    gstreamer-va-1.0
    gmodule-2.0
)

find_package(OpenCL REQUIRED)

# Define Target: MODULE is preferred for GStreamer plugins
add_library(gstthzoclblur MODULE gstthzoclblur.c)

# Add definitions
target_compile_definitions(gstthzoclblur PRIVATE 
    CL_TARGET_OPENCL_VERSION=300
    KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/blur.cl"
)

# Include directories
target_include_directories(gstthzoclblur PRIVATE 
    ${GST_INCLUDE_DIRS} 
    ${OpenCL_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(gstthzoclblur PRIVATE
    ${GST_LIBRARIES} 
    ${OpenCL_LIBRARIES}
)

# Ensure the output file name follows GStreamer plugin naming conventions
set_target_properties(gstthzoclblur PROPERTIES 
    PREFIX "lib"
    OUTPUT_NAME "gstthzoclblur"
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

message(STATUS "OCL Blur plugin will be installed to: ${GST_PLUGINS_DIR}")

# Install the library to the detected GStreamer path
install(TARGETS gstthzoclblur DESTINATION ${GST_PLUGINS_DIR})