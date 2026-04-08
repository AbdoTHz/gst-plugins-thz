cmake_minimum_required(VERSION 3.10)
project(gst-thzvaapiblur C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-allocators-1.0 gstreamer-va-1.0)
pkg_check_modules(VAAPI REQUIRED libva libva-drm)

find_package(OpenCL REQUIRED)

add_library(gstthzvaapiblur SHARED gstthzvaapiblur.c)

# Add the path where OpenCL headers for Intel Interop are found
target_include_directories(gstthzvaapiblur PRIVATE 
    ${GST_INCLUDE_DIRS} 
    ${VAAPI_INCLUDE_DIRS} 
    ${OpenCL_INCLUDE_DIRS}
)

target_link_libraries(gstthzvaapiblur 
    ${GST_LIBRARIES} 
    ${GST_VA_LIBRARIES}
    ${VAAPI_LIBRARIES} 
    ${OpenCL_LIBRARIES}
)

# Define the path to the kernel file so the C code can find it
add_definitions(-DKERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/blur.cl")

install(TARGETS gstthzvaapiblur DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/gstreamer-1.0)