INCLUDE_DIRECTORIES(
    .
    gui
    ${CMAKE_CURRENT_BINARY_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

SET(SrtStreamer_SRCS
    SrtStreamerModule.hpp
    SrtStreamerModule.cpp
    SrtEncoder.hpp
    SrtEncoder.cpp
    FrameCapture.hpp
    FrameCapture.cpp
    gui/SrtStreamerWindow.hpp
    gui/SrtStreamerWindow.cpp
)

SET(SrtStreamer_UIS
    gui/SrtStreamerWindow.ui
)

SET(SrtStreamer_RES
    ../resources/SrtStreamer.qrc
)

IF(${QT_VERSION_MAJOR} EQUAL "5")
    QT5_WRAP_UI(SrtStreamer_UIS_H ${SrtStreamer_UIS})
    QT5_ADD_RESOURCES(SrtStreamer_RES_CXX ${SrtStreamer_RES})
ELSE()
    QT_WRAP_UI(SrtStreamer_UIS_H ${SrtStreamer_UIS})
    QT_ADD_RESOURCES(SrtStreamer_RES_CXX ${SrtStreamer_RES})
ENDIF()

IF(SRTSTREAMER_STATIC)
    # ---- Static plugin (inside Stellarium tree) ----
    LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/src ${FFMPEG_LIBRARY_DIRS})

    ADD_LIBRARY(SrtStreamer-static STATIC ${SrtStreamer_SRCS} ${SrtStreamer_UIS_H} ${SrtStreamer_RES_CXX})
    SET_TARGET_PROPERTIES(SrtStreamer-static PROPERTIES OUTPUT_NAME "SrtStreamer")
    SET_TARGET_PROPERTIES(SrtStreamer-static PROPERTIES COMPILE_FLAGS "-DQT_STATICPLUGIN")
    SET_TARGET_PROPERTIES(SrtStreamer-static PROPERTIES FOLDER "plugins/SrtStreamer")

    FIND_PACKAGE(OpenGL REQUIRED)
    TARGET_LINK_LIBRARIES(SrtStreamer-static
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::OpenGL
        Qt${QT_VERSION_MAJOR}::Concurrent
        OpenGL::GL
        ${FFMPEG_LIBRARIES}
    )

    ADD_DEPENDENCIES(AllStaticPlugins SrtStreamer-static)

ELSE()
    # ---- Dynamic plugin (standalone, Linux) ----
    INCLUDE_DIRECTORIES(
        ${STELROOT}/src
        ${STELROOT}/src/core
        ${STELROOT}/src/core/modules
        ${STELROOT}/src/gui
        ${STELROOT}/src/external
        ${STELROOT}/src/external/qtcompress
    )

    LINK_DIRECTORIES(${FFMPEG_LIBRARY_DIRS})

    ADD_LIBRARY(SrtStreamer-dynamic MODULE ${SrtStreamer_SRCS} ${SrtStreamer_UIS_H} ${SrtStreamer_RES_CXX})

    TARGET_COMPILE_DEFINITIONS(SrtStreamer-dynamic PRIVATE QT_DYNAMICPLUGIN)

    TARGET_LINK_LIBRARIES(SrtStreamer-dynamic PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::OpenGL
        OpenGL::GL
        ${FFMPEG_LIBRARIES}
    )

    IF(QT_VERSION_MAJOR EQUAL 6)
        TARGET_LINK_LIBRARIES(SrtStreamer-dynamic PRIVATE Qt6::OpenGLWidgets)
    ENDIF()

    SET_TARGET_PROPERTIES(SrtStreamer-dynamic PROPERTIES
        AUTOMOC TRUE
        AUTOUIC TRUE
        PREFIX "lib"
        OUTPUT_NAME "SrtStreamer"
    )

    # Install
    IF(UNIX AND NOT APPLE)
        SET(STEL_PLUGIN_DIR "$ENV{HOME}/.stellarium/modules/SrtStreamer")
    ELSEIF(APPLE)
        SET(STEL_PLUGIN_DIR "$ENV{HOME}/Library/Application Support/Stellarium/modules/SrtStreamer")
    ENDIF()

    INSTALL(TARGETS SrtStreamer-dynamic LIBRARY DESTINATION "${STEL_PLUGIN_DIR}")
ENDIF()
