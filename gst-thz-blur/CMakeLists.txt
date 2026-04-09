cmake_minimum_required(VERSION 3.10)
project(gst-thzoclblur C)

# Use standard C99 or higher for GStreamer
set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)

# Combine all GStreamer dependencies into one check for simplicity
# Ensure gstreamer-va-1.0 is present
pkg_check_modules(GST REQUIRED 
    gstreamer-1.0 
    gstreamer-base-1.0
    gstreamer-video-1.0 
    gstreamer-allocators-1.0 
    gstreamer-va-1.0
    gmodule-2.0
)

find_package(OpenCL REQUIRED)

# Create the library
add_library(gstthzoclblur SHARED gstthzoclblur.c)

# Add definitions
# Note: Escaping the quotes for KERNEL_PATH so the C code receives a valid string
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
# Explicitly using GST_LIBRARIES which now contains gstva from the pkg_check above
target_link_libraries(gstthzoclblur 
    ${GST_LIBRARIES} 
    ${OpenCL_LIBRARIES}
)

# Optional: Ensure the output file name follows GStreamer plugin naming conventions
set_target_properties(gstthzoclblur PROPERTIES PREFIX "lib")