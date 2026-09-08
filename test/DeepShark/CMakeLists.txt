target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        DeepSharkConnectionMonitorTest.cc
        DeepSharkConnectionMonitorTest.h
        AIDetectionReceiverTest.cc
        AIDetectionReceiverTest.h
        DeepSharkAuvControllerTest.cc
        DeepSharkAuvControllerTest.h
        DeepSharkVideoControllerTest.cc
        DeepSharkVideoControllerTest.h
        ThrusterDirectControlControllerTest.cc
        ThrusterDirectControlControllerTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
