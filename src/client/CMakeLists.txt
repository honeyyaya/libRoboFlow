set(CLIENT_SOURCES
    connect_info.cpp
    connect_cb.cpp
    stream_param.cpp
    stream_cb.cpp
    infrastructure.cpp
    lifecycle.cpp
    stream.cpp
    signaling.cpp
    runtime_log.cpp
)

# -------------------------------------------------------------------- impl 层
# 真实拉流、信令在开启 RFLOW_CLIENT_ENABLE_WEBRTC_IMPL 时编译；WebRTC 工厂与 init 在 core/rtc
if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    list(APPEND CLIENT_SOURCES
        impl/signaling/signaling_client.cpp
        impl/webrtc/webrtc_frame_converter.cpp
        impl/webrtc/webrtc_pull_stream.cpp
        impl/webrtc/webrtc_pull_manager.cpp
    )
endif()

if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_rtc_webrtc>)
else()
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_rtc_stub>)
endif()

if(RFLOW_BUILD_SHARED)
    add_library(rflow_client SHARED ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        ${_RRTC_OBJS}
    )
else()
    add_library(rflow_client STATIC ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        ${_RRTC_OBJS}
    )
endif()

set_target_properties(rflow_client PROPERTIES
    OUTPUT_NAME "rflow_client"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
)

target_include_directories(rflow_client
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/impl
)

# 与预编译 libwebrtc 链接（定义在 src/core/CMakeLists.txt）
if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    target_include_directories(rflow_client SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/include/webrtc
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/abseil-cpp
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/libyuv/include
    )
    target_link_libraries(rflow_client PRIVATE rflow_webrtc_prebuilt)
    target_compile_definitions(rflow_client PRIVATE
        RFLOW_RTC_WEBRTC_PEER_CONNECTION_API=1
        WEBRTC_POSIX=1
    )
    if(ANDROID)
        target_compile_definitions(rflow_client PRIVATE WEBRTC_ANDROID=1)
        target_link_libraries(rflow_client PRIVATE mediandk android log OpenSLES EGL GLESv2)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(rflow_client PRIVATE WEBRTC_LINUX=1)
        find_package(Threads REQUIRED)
        target_link_libraries(rflow_client PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    endif()
endif()

target_link_libraries(rflow_client PRIVATE rflow_warnings)
# TODO: 后续通过 version script (GNU) 精准导出公开符号
