cmake_minimum_required(VERSION 3.10)
project(gst-thz-meta VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- CRITICAL FIX FOR THE ERROR ---
# GStreamer's GST_PLUGIN_DEFINE macro requires the PACKAGE definition
add_definitions(-DPACKAGE="gst-thz-meta")

find_package(PkgConfig REQUIRED)

# Dependencies
pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(SDBUS REQUIRED sdbus-c++)

# Target: gstthzmeta
add_library(gstthzmeta SHARED gstthzmeta.cpp)

target_include_directories(gstthzmeta PRIVATE 
    ${GST_INCLUDE_DIRS} 
    ${GST_BASE_INCLUDE_DIRS}
    ${SDBUS_INCLUDE_DIRS}
)

target_link_libraries(gstthzmeta PRIVATE 
    ${GST_LIBRARIES} 
    ${GST_BASE_LIBRARIES}
    ${SDBUS_LIBRARIES}
    pthread
)

set_target_properties(gstthzmeta PROPERTIES PREFIX "lib")

# Installation logic
execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pluginsdir gstreamer-1.0
                OUTPUT_VARIABLE GST_PLUGINS_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT GST_PLUGINS_DIR)
    set(GST_PLUGINS_DIR "/usr/lib/x86_64-linux-gnu/gstreamer-1.0")
endif()

install(TARGETS gstthzmeta DESTINATION ${GST_PLUGINS_DIR})