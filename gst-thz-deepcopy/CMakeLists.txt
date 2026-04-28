cmake_minimum_required(VERSION 3.10)
project(ThzDeepCopyPlugin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find GStreamer Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-video-1.0)
pkg_check_modules(GLIB REQUIRED glib-2.0)

# 2. Include Headers
include_directories(
    ${GST_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    .
)

# 3. Build the Plugin Library
# This will create 'gstthzdeepcopy.so'
add_library(gstthzdeepcopy SHARED gstthzdeepcopy.cpp)

target_link_libraries(gstthzdeepcopy
    ${GST_LIBRARIES}
    ${GST_VIDEO_LIBRARIES}
)

# 4. Remove 'lib' prefix to follow GStreamer naming convention
set_target_properties(gstthzdeepcopy PROPERTIES PREFIX "")

message(STATUS "Plugin configuration complete. Run 'make' to build gstthzdeepcopy.so")