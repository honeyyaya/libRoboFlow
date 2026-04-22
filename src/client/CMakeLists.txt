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
        impl/signaling/signaling_client.cpp
        impl/webrtc/webrtc_factory.cpp
        impl/webrtc/webrtc_pull_stream.cpp
        impl/webrtc/webrtc_pull_manager.cpp
    )
    if(ANDROID)
        list(APPEND CLIENT_SOURCES
            impl/decoder/android/android_hw_video_decoder_factory.cpp
            impl/decoder/android/android_mediacodec_video_decoder.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/impl
)

if(ROBRT_CLIENT_ENABLE_WEBRTC_IMPL)
    # ---------------------------------------------------------------- webrtc 依赖定位
    # 目录布局（新增）：
    #   dependences/lib/android/arm64-v8a/libwebrtc.a
    #   dependences/lib/linux/<arch>/libwebrtc.a
    # include 仍共用：include/webrtc
    set(ROBRT_DEPS_ROOT ${CMAKE_SOURCE_DIR}/dependences)
    set(ROBRT_WEBRTC_INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/include/webrtc)

    if(ANDROID)
        # ANDROID_ABI 由 NDK toolchain 注入（arm64-v8a / armeabi-v7a / x86_64 / ...）
        if(NOT ANDROID_ABI)
            set(ANDROID_ABI "arm64-v8a")
        endif()
        set(ROBRT_WEBRTC_LIB_DIR ${ROBRT_DEPS_ROOT}/lib/android/${ANDROID_ABI})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # 目前仅落了 arm64；扩平台时在 dependences/lib/linux/<arch>/ 下追加即可。
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _robrt_sys_arch)
        if(_robrt_sys_arch STREQUAL "aarch64" OR _robrt_sys_arch STREQUAL "arm64")
            set(_robrt_lib_arch "arm64")
        else()
            set(_robrt_lib_arch "${_robrt_sys_arch}")
        endif()
        set(ROBRT_WEBRTC_LIB_DIR ${ROBRT_DEPS_ROOT}/lib/linux/${_robrt_lib_arch})
    else()
        message(FATAL_ERROR
            "ROBRT_CLIENT_ENABLE_WEBRTC_IMPL: 仅支持 Android / Linux (当前 ${CMAKE_SYSTEM_NAME})")
    endif()

    set(ROBRT_WEBRTC_LIB ${ROBRT_WEBRTC_LIB_DIR}/libwebrtc.a)
    if(NOT EXISTS ${ROBRT_WEBRTC_LIB})
        message(FATAL_ERROR
            "找不到 libwebrtc.a: ${ROBRT_WEBRTC_LIB}\n"
            "请在 dependences/lib/<platform>/<arch>/ 下放置对应预编译产物。")
    endif()
    message(STATUS "robrt webrtc lib : ${ROBRT_WEBRTC_LIB}")

    add_library(robrt_webrtc_prebuilt STATIC IMPORTED)
    set_target_properties(robrt_webrtc_prebuilt PROPERTIES
        IMPORTED_LOCATION ${ROBRT_WEBRTC_LIB}
        INTERFACE_INCLUDE_DIRECTORIES
            "${ROBRT_WEBRTC_INCLUDE_ROOT};${ROBRT_WEBRTC_INCLUDE_ROOT}/third_party/abseil-cpp;${ROBRT_WEBRTC_INCLUDE_ROOT}/third_party/libyuv/include"
    )

    target_link_libraries(robrt_client PRIVATE robrt_webrtc_prebuilt)
    target_compile_definitions(robrt_client PRIVATE WEBRTC_POSIX=1)

    if(ANDROID)
        target_compile_definitions(robrt_client PRIVATE WEBRTC_ANDROID=1)
        # NDK 系统库：MediaCodec / android / log；libwebrtc 内部也会引用这些。
        target_link_libraries(robrt_client PRIVATE mediandk android log OpenSLES EGL GLESv2)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(robrt_client PRIVATE WEBRTC_LINUX=1)
        find_package(Threads REQUIRED)
        target_link_libraries(robrt_client PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        # TODO: 如 Linux 侧 libwebrtc 依赖 X11 / pulse / alsa，可在此追加。
    endif()
endif()

target_link_libraries(robrt_client PRIVATE robrt_warnings)
# TODO: 后续通过 version script (GNU) 精准导出公开符号
