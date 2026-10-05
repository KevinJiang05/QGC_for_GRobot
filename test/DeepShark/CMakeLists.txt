target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE DeepSharkConnectionMonitorTest.cc
            DeepSharkConnectionMonitorTest.h
            AIDetectionReceiverTest.cc
            AIDetectionReceiverTest.h
            DeepSharkAuvControllerTest.cc
            DeepSharkAuvControllerTest.h
            DeepSharkVideoControllerTest.cc
            DeepSharkVideoControllerTest.h
            DeepSharkUILayoutTest.cc
            DeepSharkUILayoutTest.h
            ThrusterDirectControlControllerTest.cc
            ThrusterDirectControlControllerTest.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_qgc_test(AIDetectionReceiverTest LABELS Unit DeepShark)

add_qgc_test(DeepSharkAuvControllerTest LABELS Unit DeepShark)

add_qgc_test(DeepSharkConnectionMonitorTest LABELS Unit DeepShark)

add_qgc_test(DeepSharkVideoControllerTest LABELS Unit DeepShark ENV_MODIFICATION "QGC_TEST_ENABLE_GSTREAMER=set:1")

add_qgc_test(ThrusterDirectControlControllerTest LABELS Unit DeepShark)

add_qgc_test(
    DeepSharkUILayoutTest
    LABELS
    Integration
    DeepShark
    UI
    ENV_MODIFICATION
    "QGC_TEST_ENABLE_GSTREAMER=set:1"
)
