# Stellarium SRT Streamer Plugin
#
# Two build modes:
#
# 1) STATIC (inside Stellarium source tree):
#    Copy/symlink this directory into stellarium/plugins/SrtStreamer
#    then add ADD_PLUGIN(SrtStreamer 1) to Stellarium's CMakeLists.txt.
#    All Qt/FFmpeg/OpenGL variables are inherited from the parent build.
#
# 2) STANDALONE (dynamic plugin, Linux only):
#    cmake -DSTELROOT=/path/to/stellarium -DFFMPEG_ROOT=/path/to/ffmpeg ..
#    Produces libSrtStreamer.so for ~/.stellarium/modules/SrtStreamer/

SET(SRTSTREAMER_VERSION "1.1.0")

# Detect whether we're inside Stellarium's build tree
if(DEFINED STELLARIUM_PLUGINS)
    # --- Static plugin mode (inside Stellarium tree) ---
    set(SRTSTREAMER_STATIC TRUE)
    message(STATUS "SrtStreamer: building as STATIC plugin inside Stellarium tree")
else()
    # --- Standalone mode (dynamic plugin) ---
    set(SRTSTREAMER_STATIC FALSE)
    message(STATUS "SrtStreamer: building as STANDALONE dynamic plugin")

    cmake_minimum_required(VERSION 3.16)
    project(SrtStreamer VERSION ${SRTSTREAMER_VERSION} LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)

    # Stellarium source tree (for headers)
    if(NOT DEFINED ENV{STELROOT} AND NOT DEFINED STELROOT)
        message(FATAL_ERROR
            "Set STELROOT (env or CMake var) to your Stellarium source tree root.")
    endif()
    if(DEFINED ENV{STELROOT} AND NOT DEFINED STELROOT)
        set(STELROOT "$ENV{STELROOT}")
    endif()
    message(STATUS "Using Stellarium source tree: ${STELROOT}")

    # Qt
    set(ENABLE_QT6 1 CACHE BOOL "Try Qt6 first; fall back to Qt5 if not found.")
    if(ENABLE_QT6)
        find_package(Qt6 COMPONENTS Core QUIET)
    endif()
    if(Qt6_FOUND)
        set(QT_VERSION_MAJOR 6)
        find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
    else()
        set(QT_VERSION_MAJOR 5)
        find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
    endif()

    # OpenGL
    find_package(OpenGL REQUIRED)
endif()

# FFmpeg (needed in both modes)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT DEFINED FFMPEG_ROOT)
    pkg_check_modules(AVCODEC  REQUIRED libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVUTIL   REQUIRED libavutil)
    pkg_check_modules(SWSCALE  REQUIRED libswscale)
    set(FFMPEG_INCLUDE_DIRS ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS}
                            ${AVUTIL_INCLUDE_DIRS}  ${SWSCALE_INCLUDE_DIRS})
    set(FFMPEG_LIBRARIES    ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES}
                            ${AVUTIL_LIBRARIES}  ${SWSCALE_LIBRARIES})
    set(FFMPEG_LIBRARY_DIRS ${AVCODEC_LIBRARY_DIRS} ${AVFORMAT_LIBRARY_DIRS}
                            ${AVUTIL_LIBRARY_DIRS}  ${SWSCALE_LIBRARY_DIRS})
else()
    if(NOT DEFINED FFMPEG_ROOT)
        message(FATAL_ERROR "Set -DFFMPEG_ROOT=<path> to FFmpeg install prefix.")
    endif()
    set(FFMPEG_INCLUDE_DIRS ${FFMPEG_ROOT}/include)
    set(FFMPEG_LIBRARY_DIRS ${FFMPEG_ROOT}/lib)
    find_library(LIB_AVCODEC  avcodec  PATHS ${FFMPEG_LIBRARY_DIRS} REQUIRED)
    find_library(LIB_AVFORMAT avformat PATHS ${FFMPEG_LIBRARY_DIRS} REQUIRED)
    find_library(LIB_AVUTIL   avutil   PATHS ${FFMPEG_LIBRARY_DIRS} REQUIRED)
    find_library(LIB_SWSCALE  swscale  PATHS ${FFMPEG_LIBRARY_DIRS} REQUIRED)
    set(FFMPEG_LIBRARIES ${LIB_AVCODEC} ${LIB_AVFORMAT} ${LIB_AVUTIL} ${LIB_SWSCALE})
endif()

ADD_SUBDIRECTORY(src)
