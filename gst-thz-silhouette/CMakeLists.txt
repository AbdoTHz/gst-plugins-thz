cmake_minimum_required(VERSION 3.10)
project(gst-thz-silhouette VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DGST_USE_UNSTABLE_API)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GST_VA REQUIRED gstreamer-va-1.0)
pkg_check_modules(OPENCV REQUIRED opencv4)

# 1. Include paths
include_directories(/opt/intel/dlstreamer/include)
include_directories(/opt/intel/dlstreamer/gstreamer/include/gstreamer-1.0)

# 2. Library search paths
link_directories(/opt/intel/dlstreamer/gstreamer/lib)
link_directories(/opt/intel/dlstreamer/lib)

add_library(gstthzsilhouette SHARED gstthzsilhouette.cpp)

target_include_directories(gstthzsilhouette PRIVATE 
    ${GST_INCLUDE_DIRS} 
    ${GST_VIDEO_INCLUDE_DIRS}
    ${GST_VA_INCLUDE_DIRS}
    ${OPENCV_INCLUDE_DIRS}
)

# 3. Link the specific libraries found by grep
target_link_libraries(gstthzsilhouette PRIVATE 
    ${GST_LIBRARIES} 
    ${GST_VIDEO_LIBRARIES} 
    ${GST_VA_LIBRARIES}
    ${OPENCV_LIBRARIES}
    gstanalytics-1.0
    gstvideoanalytics
    dlstreamer_gst_meta
    gstdlstreamer_elements
)

set_target_properties(gstthzsilhouette PROPERTIES PREFIX "" OUTPUT_NAME "gstthzsilhouette")