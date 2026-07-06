message(STATUS "QGC: Adding DeepShark Custom Plugin")

set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    APPEND PROPERTY COMPILE_DEFINITIONS
    QGC_CUSTOM_BUILD
    CUSTOMHEADER="DeepSharkPlugin.h"
    CUSTOMCLASS=DeepSharkPlugin
)

list(APPEND CUSTOM_RESOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/custom.qrc
)
set(QGC_RESOURCES ${QGC_RESOURCES} ${CUSTOM_RESOURCES} CACHE STRING "Paths to .qrc Resources" FORCE)

set(CUSTOM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkPlugin.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkPlugin.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkAuvController.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkAuvController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkVideoSettings.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkVideoSettings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkVideoController.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeepSharkVideoController.h
    CACHE INTERNAL "" FORCE
)

set(CUSTOM_INCLUDE_DIRECTORIES
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/VideoManager/VideoReceiver
    CACHE INTERNAL "" FORCE
)
