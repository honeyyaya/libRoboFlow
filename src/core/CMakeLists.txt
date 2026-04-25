# src/core — 信令 / 线程池 / POSIX I/O + RTC（rtc stub 与可选 libwebrtc 实现分离，避免 service 链入 libwebrtc）
add_library(rflow_core OBJECT
    signal/protocol.cpp
    signal/signal_stub.cpp
    thread/thread_pool.cpp
    net/posix_io.cpp
)

add_library(rflow_rtc_stub OBJECT rtc/rtc_stub.cpp)

# 独立信令服务器 (TCP + epoll)，供 apps/signaling_server 链接；
# 仅依赖 core/signal/protocol + core/net/posix_io，不需 libwebrtc。
add_library(rflow_signal_server OBJECT signal/server.cpp)
target_include_directories(rflow_signal_server
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(rflow_signal_server PRIVATE rflow_warnings)

set(_RFLOW_NEED_WEBRTC_IMPL OFF)
if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL AND RFLOW_BUILD_CLIENT)
    set(_RFLOW_NEED_WEBRTC_IMPL ON)
endif()
if(RFLOW_SERVICE_ENABLE_WEBRTC_IMPL AND RFLOW_BUILD_SERVICE)
    set(_RFLOW_NEED_WEBRTC_IMPL ON)
endif()

if(_RFLOW_NEED_WEBRTC_IMPL)
    set(_RTC_W_SRCS
        rtc/rtc_factory_common.cpp
        rtc/peer_connection_factory.cpp
        rtc/peer_connection_factory_deps.cpp
        rtc/hw/backend_registry.cpp
        rtc/shims/sequence_checker_expectation_to_string.cpp
        rtc/shims/sme_abi_stubs.c
    )
    if(ANDROID)
        list(APPEND _RTC_W_SRCS
            rtc/hw/android/video_decoder_factory.cpp
            rtc/hw/android/mediacodec_video_decoder.cpp
            rtc/hw/android/native_dec_frame_buffer.cpp
        )
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        list(APPEND _RTC_W_SRCS
            rtc/hw/rockchip_mpp/h264_decoder.cpp
            rtc/hw/rockchip_mpp/h264_encoder.cpp
            rtc/hw/rockchip_mpp/mjpeg_decoder.cpp
            rtc/hw/rockchip_mpp/native_dec_frame_buffer.cpp
            rtc/hw/rockchip_mpp/video_decoder_factory.cpp
            rtc/hw/rockchip_mpp/video_encoder_factory.cpp
        )
    endif()

    add_library(rflow_rtc_webrtc OBJECT ${_RTC_W_SRCS})

    set(RFLOW_DEPS_ROOT ${CMAKE_SOURCE_DIR}/dependences)
    set(RFLOW_WEBRTC_INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/include/webrtc)

    if(ANDROID)
        if(NOT ANDROID_ABI)
            set(ANDROID_ABI "arm64-v8a")
        endif()
        set(RFLOW_WEBRTC_LIB_DIR ${RFLOW_DEPS_ROOT}/lib/android/${ANDROID_ABI})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _rflow_sys_arch)
        if(_rflow_sys_arch STREQUAL "aarch64" OR _rflow_sys_arch STREQUAL "arm64")
            set(_rflow_lib_arch "arm64")
        else()
            set(_rflow_lib_arch "${_rflow_sys_arch}")
        endif()
        set(RFLOW_WEBRTC_LIB_DIR ${RFLOW_DEPS_ROOT}/lib/linux/${_rflow_lib_arch})
    else()
        message(FATAL_ERROR
            "RFLOW_CLIENT_ENABLE_WEBRTC_IMPL: 仅支持 Android / Linux 交叉构建 (当前 ${CMAKE_SYSTEM_NAME})")
    endif()

    set(RFLOW_WEBRTC_LIB ${RFLOW_WEBRTC_LIB_DIR}/libwebrtc.a)
    if(NOT EXISTS ${RFLOW_WEBRTC_LIB})
        message(FATAL_ERROR
            "找不到 libwebrtc.a: ${RFLOW_WEBRTC_LIB}\n"
            "请在 dependences/lib/<platform>/<arch>/ 下放置对应预编译产物。")
    endif()
    message(STATUS "rflow webrtc (core rtc) : ${RFLOW_WEBRTC_LIB}")

    add_library(rflow_webrtc_prebuilt STATIC IMPORTED GLOBAL)
    set_target_properties(rflow_webrtc_prebuilt PROPERTIES
        IMPORTED_LOCATION ${RFLOW_WEBRTC_LIB}
        INTERFACE_INCLUDE_DIRECTORIES
            "${RFLOW_WEBRTC_INCLUDE_ROOT};${RFLOW_WEBRTC_INCLUDE_ROOT}/third_party/abseil-cpp;${RFLOW_WEBRTC_INCLUDE_ROOT}/third_party/libyuv/include"
    )

    target_link_libraries(rflow_rtc_webrtc PRIVATE rflow_webrtc_prebuilt)
    target_compile_definitions(rflow_rtc_webrtc PRIVATE
        RFLOW_RTC_WEBRTC_PEER_CONNECTION_API=1
        WEBRTC_POSIX=1
    )
    if(ANDROID)
        target_compile_definitions(rflow_rtc_webrtc PRIVATE WEBRTC_ANDROID=1)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(rflow_rtc_webrtc PRIVATE WEBRTC_LINUX=1)
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        target_compile_definitions(rflow_rtc_webrtc PRIVATE
            WEBRTC_DEMO_HAVE_ROCKCHIP_MPP=1)
        # 调用方 (service/impl 等) 编译期也能看到该宏
        target_compile_definitions(rflow_rtc_webrtc INTERFACE
            WEBRTC_DEMO_HAVE_ROCKCHIP_MPP=1)
    endif()
endif()

# include：线程 / 信令子目录，便于 "thread_pool.h" / "signal.h"
target_include_directories(rflow_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(rflow_rtc_stub
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc
)

if(_RFLOW_NEED_WEBRTC_IMPL)
    target_include_directories(rflow_rtc_webrtc
        PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/rtc
    )
    target_link_libraries(rflow_rtc_webrtc PRIVATE rflow_warnings)
    # libwebrtc headers: PortInterface::Network() vs class Network trips GCC [-fpermissive].
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rflow_rtc_webrtc PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
    endif()
    if(ANDROID)
        target_link_libraries(rflow_rtc_webrtc PRIVATE mediandk android log OpenSLES EGL GLESv2)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        target_link_libraries(rflow_rtc_webrtc PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    endif()
else()
    target_link_libraries(rflow_rtc_stub PRIVATE rflow_warnings)
endif()
target_link_libraries(rflow_core PRIVATE rflow_warnings)
