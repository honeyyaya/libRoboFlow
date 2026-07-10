# src/core — 信令 / 线程池 / POSIX I/O + RTC（rtc stub 与可选 libwebrtc 实现分离，避免 service 链入 libwebrtc）
# runtime_knobs.cpp 放在 rflow_core 而不是 rflow_core_runtime：所有 core/*.cpp（tcp_session、
# backend_registry 等）都可能读 knob，统一放底层避免链接环。signaling_server 仅链 rflow_core
# 也需要 runtime_knobs 实现存在。
add_library(rflow_core OBJECT
    signal/protocol.cpp
    signal/session_transport.cpp
    signal/signal_stub.cpp
    signal/tcp_session.cpp
    thread/thread_pool.cpp
    net/posix_io.cpp
    runtime/runtime_knobs.cpp
    rtc/congestion/congestion_policy_default.cpp
)

# rflow_core_runtime: SDK 启停编排（thread/rtc/signal 三件套），仅供 client/service 拼入；
# 因依赖 rflow::rtc::initialize/shutdown，独立目标可避免无 RTC 的目标（signaling_server）误吃。
add_library(rflow_core_runtime OBJECT runtime/infra_lifecycle.cpp)
target_include_directories(rflow_core_runtime
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/core
)
target_link_libraries(rflow_core_runtime PRIVATE rflow_warnings)

add_library(rflow_rtc_stub OBJECT rtc/rtc_stub.cpp)

# 独立信令服务器 (TCP + epoll)，供 apps/signaling_server 链接；
# 仅依赖 core/signal/protocol + core/net/posix_io，不需 libwebrtc。
add_library(rflow_signal_server OBJECT signal/server.cpp)
target_include_directories(rflow_signal_server
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
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
    set(RFLOW_DEPS_ROOT ${CMAKE_SOURCE_DIR}/dependences)
    set(RFLOW_WEBRTC_INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/include/webrtc)

    if(ANDROID)
        if(NOT ANDROID_ABI)
            set(ANDROID_ABI "arm64-v8a")
        endif()
        set(RFLOW_WEBRTC_LIB_DIR ${RFLOW_DEPS_ROOT}/lib/android/${ANDROID_ABI})
        set(_rflow_webrtc_variant "android")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _rflow_sys_arch)
        if(_rflow_sys_arch STREQUAL "aarch64" OR _rflow_sys_arch STREQUAL "arm64")
            set(_rflow_lib_arch "arm64")
        else()
            set(_rflow_lib_arch "${_rflow_sys_arch}")
        endif()
        # linux/arm64：release/ 与 debug/ 各一份 libwebrtc.a，与 CMAKE_BUILD_TYPE 对齐。
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_rflow_webrtc_variant "debug")
        else()
            set(_rflow_webrtc_variant "release")
        endif()
        set(RFLOW_WEBRTC_LIB_DIR
            ${RFLOW_DEPS_ROOT}/lib/linux/${_rflow_lib_arch}/${_rflow_webrtc_variant})
        if(NOT EXISTS ${RFLOW_WEBRTC_LIB_DIR}/libwebrtc.a)
            set(RFLOW_WEBRTC_LIB_DIR ${RFLOW_DEPS_ROOT}/lib/linux/${_rflow_lib_arch})
            set(_rflow_webrtc_variant "legacy")
        endif()
    else()
        message(FATAL_ERROR
            "RFLOW_CLIENT_ENABLE_WEBRTC_IMPL: 仅支持 Android / Linux 交叉构建 (当前 ${CMAKE_SYSTEM_NAME})")
    endif()

    set(RFLOW_WEBRTC_LIB ${RFLOW_WEBRTC_LIB_DIR}/libwebrtc.a)
    if(NOT EXISTS ${RFLOW_WEBRTC_LIB})
        message(FATAL_ERROR
            "找不到 libwebrtc.a: ${RFLOW_WEBRTC_LIB}\n"
            "Linux arm64 请放置 dependences/lib/linux/arm64/{release,debug}/libwebrtc.a；"
            "其他平台见 dependences/lib/<platform>/<arch>/。")
    endif()
    message(STATUS "rflow webrtc (core rtc) : ${RFLOW_WEBRTC_LIB} (variant=${_rflow_webrtc_variant}, CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})")

    set(_RTC_DOMAIN_SRCS
        rtc/rtc_factory_common.cpp
        rtc/rtc_ice_config.cpp
        rtc/peer_connection_factory.cpp
        rtc/peer_connection_factory_deps.cpp
        rtc/inbound_video_stats_aggregation.cpp
        rtc/outbound_video_stats_aggregation.cpp
        rtc/ice_nominated_pair_rtt.cpp
        rtc/rtc_sync_stats.cpp
        rtc/sdp_observers.cpp
        rtc/stats_observer.cpp
        rtc/video_codec_preferences.cpp
        rtc/hw/backend_registry.cpp
        rtc/shims/sme_abi_stubs.c
    )
    # Release 预编译 lib 缺 ExpectationToString；Debug 包已内置，勿重复链 shim。
    if(NOT _rflow_webrtc_variant STREQUAL "debug")
        list(APPEND _RTC_DOMAIN_SRCS rtc/shims/sequence_checker_expectation_to_string.cpp)
    endif()
    set(_RTC_PLATFORM_ANDROID_SRCS)
    set(_RTC_PLATFORM_ROCKCHIP_SRCS)
    if(ANDROID)
        list(APPEND _RTC_PLATFORM_ANDROID_SRCS
            platform/android/video_decoder_factory.cpp
            platform/android/mediacodec_video_decoder.cpp
            platform/android/native_dec_frame_buffer.cpp
        )
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        list(APPEND _RTC_PLATFORM_ROCKCHIP_SRCS
            platform/rockchip/backend_capabilities.cpp
            platform/rockchip/h264_decoder.cpp
            platform/rockchip/h264_encoder.cpp
            platform/rockchip/mpp_hardware_probe.cpp
            platform/rockchip/mjpeg_decoder.cpp
            platform/rockchip/native_dec_frame_buffer.cpp
            platform/rockchip/video_decoder_factory.cpp
            platform/rockchip/video_encoder_factory.cpp
        )
    endif()

    add_library(rflow_core_rtc_domain OBJECT ${_RTC_DOMAIN_SRCS})
    if(ANDROID)
        add_library(rflow_core_platform_android OBJECT ${_RTC_PLATFORM_ANDROID_SRCS})
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        add_library(rflow_core_platform_rockchip OBJECT ${_RTC_PLATFORM_ROCKCHIP_SRCS})
    endif()

    add_library(rflow_webrtc_prebuilt STATIC IMPORTED GLOBAL)
    set_target_properties(rflow_webrtc_prebuilt PROPERTIES
        IMPORTED_LOCATION ${RFLOW_WEBRTC_LIB}
        INTERFACE_INCLUDE_DIRECTORIES
            "${RFLOW_WEBRTC_INCLUDE_ROOT};${RFLOW_WEBRTC_INCLUDE_ROOT}/third_party/abseil-cpp;${RFLOW_WEBRTC_INCLUDE_ROOT}/third_party/libyuv/include"
    )

    set(_RFLOW_RTC_TARGETS rflow_core_rtc_domain)
    if(ANDROID)
        list(APPEND _RFLOW_RTC_TARGETS rflow_core_platform_android)
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        list(APPEND _RFLOW_RTC_TARGETS rflow_core_platform_rockchip)
    endif()
    foreach(_rtc_target IN LISTS _RFLOW_RTC_TARGETS)
        target_link_libraries(${_rtc_target} PRIVATE rflow_webrtc_prebuilt rflow_warnings)
        target_include_directories(${_rtc_target}
            PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/src/common
                ${CMAKE_SOURCE_DIR}/src/core
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/rtc
        )
        target_compile_definitions(${_rtc_target} PRIVATE
            RFLOW_RTC_WEBRTC_PEER_CONNECTION_API=1
            WEBRTC_POSIX=1
        )
    endforeach()
    if(ANDROID)
        foreach(_rtc_target IN LISTS _RFLOW_RTC_TARGETS)
            target_compile_definitions(${_rtc_target} PRIVATE WEBRTC_ANDROID=1)
        endforeach()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        foreach(_rtc_target IN LISTS _RFLOW_RTC_TARGETS)
            target_compile_definitions(${_rtc_target} PRIVATE WEBRTC_LINUX=1)
        endforeach()
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        target_compile_definitions(rflow_core_rtc_domain PRIVATE
            RFLOW_HAVE_ROCKCHIP_MPP=1)
        target_compile_definitions(rflow_core_platform_rockchip PRIVATE
            RFLOW_HAVE_ROCKCHIP_MPP=1)
        find_path(RFLOW_MPP_INCLUDE_DIR
            NAMES mpp_buffer.h
            PATH_SUFFIXES rockchip
            PATHS /usr/include /usr/local/include
        )
        find_library(RFLOW_MPP_LIB
            NAMES rockchip_mpp mpp
            PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64
        )
        if(NOT RFLOW_MPP_INCLUDE_DIR OR NOT RFLOW_MPP_LIB)
            message(FATAL_ERROR
                "RFLOW_ENABLE_ROCKCHIP_MPP=ON but mpp headers/libs not found. "
                "Need mpp_buffer.h and librockchip_mpp/libmpp.")
        endif()
        target_include_directories(rflow_core_platform_rockchip SYSTEM PRIVATE ${RFLOW_MPP_INCLUDE_DIR})
        target_link_libraries(rflow_core_platform_rockchip PRIVATE ${RFLOW_MPP_LIB})
    endif()
endif()

# include：线程 / 信令子目录，便于 "thread_pool.h" / "signal.h"
target_include_directories(rflow_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(rflow_rtc_stub
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/rtc
)

if(_RFLOW_NEED_WEBRTC_IMPL)
    # libwebrtc headers: PortInterface::Network() vs class Network trips GCC [-fpermissive].
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        foreach(_rtc_target IN LISTS _RFLOW_RTC_TARGETS)
            target_compile_options(${_rtc_target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
        endforeach()
    endif()
    if(ANDROID)
        foreach(_rtc_target IN LISTS _RFLOW_RTC_TARGETS)
            target_link_libraries(${_rtc_target} PRIVATE mediandk android log OpenSLES EGL GLESv2)
        endforeach()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(Threads REQUIRED)
        foreach(_rtc_target IN LISTS _RFLOW_RTC_TARGETS)
            target_link_libraries(${_rtc_target} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        endforeach()
    endif()
else()
    target_link_libraries(rflow_rtc_stub PRIVATE rflow_warnings)
endif()
target_link_libraries(rflow_core PRIVATE rflow_warnings)
