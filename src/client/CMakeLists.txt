set(CLIENT_SOURCES
    connect_info.cpp
    connect_cb.cpp
    stream_param.cpp
    stream_cb.cpp
    lifecycle.cpp
    stream.cpp
    signaling.cpp
    runtime_log.cpp
)

# -------------------------------------------------------------------- impl 层
# 真实 WebRTC 拉流实现，依赖 ${CMAKE_SOURCE_DIR}/include/webrtc 预编译头与产物库。
# 默认关闭以保持 stub 构建干净；在打开 WebRTC 依赖时启用。
option(ROBRT_CLIENT_ENABLE_WEBRTC_IMPL "Enable WebRTC-backed client impl (requires include/webrtc + libwebrtc)" OFF)

if(ROBRT_CLIENT_ENABLE_WEBRTC_IMPL)
    list(APPEND CLIENT_SOURCES
        impl/signaling_client.cpp
        impl/webrtc_factory.cpp
        impl/webrtc_pull_stream.cpp
        impl/webrtc_pull_manager.cpp
    )
    if(ANDROID)
        list(APPEND CLIENT_SOURCES
            impl/android_hw_video_decoder_factory.cpp
            impl/android_mediacodec_video_decoder.cpp
        )
    endif()
endif()

if(ROBRT_BUILD_SHARED)
    add_library(robrt_client SHARED ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:robrt_common>
        $<TARGET_OBJECTS:robrt_core>
    )
else()
    add_library(robrt_client STATIC ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:robrt_common>
        $<TARGET_OBJECTS:robrt_core>
    )
endif()

set_target_properties(robrt_client PROPERTIES
    OUTPUT_NAME "robrt_client"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
)

target_include_directories(robrt_client
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

if(ROBRT_CLIENT_ENABLE_WEBRTC_IMPL)
    target_include_directories(robrt_client PRIVATE
        ${CMAKE_SOURCE_DIR}/include/webrtc
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/abseil-cpp
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/libyuv/include
    )
    target_compile_definitions(robrt_client PRIVATE WEBRTC_POSIX=1)
    if(ANDROID)
        target_compile_definitions(robrt_client PRIVATE WEBRTC_ANDROID=1)
        # TODO: 链接 libwebrtc / NDK 库（libmediandk, libandroid, liblog, libyuv）
        target_link_libraries(robrt_client PRIVATE mediandk android log)
    endif()
    # TODO: find_library(libwebrtc ...) 指向 dependences/ 中的预编译产物后再 target_link_libraries。
endif()

target_link_libraries(robrt_client PRIVATE robrt_warnings)
# TODO: 后续通过 version script (GNU) 精准导出公开符号
