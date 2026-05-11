set(SERVICE_SOURCES
    api_handles.cpp
    api_stream_objects.cpp
    lifecycle.cpp
    stream.cpp
    internal/stream_startup_policy.cpp
    internal/state_ops.cpp
    internal/service_default_params.cpp
)

# -------------------------------------------------------------------- impl 层
# WebRTC 业务实现（push_streamer / signaling_client / publisher 等）仅在显式打开
# RFLOW_SERVICE_ENABLE_WEBRTC_IMPL 时编译并链入 rflow_svc。未打开时 rflow_svc
# 保持"纯骨架"，push_video_frame 等返回 OK，不搬运媒体数据。
add_subdirectory(impl)

if(RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_core_rtc_domain>)
    if(ANDROID)
        list(APPEND _RRTC_OBJS $<TARGET_OBJECTS:rflow_core_platform_android>)
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        list(APPEND _RRTC_OBJS $<TARGET_OBJECTS:rflow_core_platform_rockchip>)
    endif()
    set(_SVC_IMPL_OBJS $<TARGET_OBJECTS:rflow_svc_impl>)
else()
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_rtc_stub>)
    set(_SVC_IMPL_OBJS "")
endif()

if(RFLOW_BUILD_SHARED)
    add_library(rflow_svc SHARED ${SERVICE_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        $<TARGET_OBJECTS:rflow_core_runtime>
        ${_RRTC_OBJS}
        ${_SVC_IMPL_OBJS}
    )
else()
    add_library(rflow_svc STATIC ${SERVICE_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        $<TARGET_OBJECTS:rflow_core_runtime>
        ${_RRTC_OBJS}
        ${_SVC_IMPL_OBJS}
    )
endif()

set_target_properties(rflow_svc PROPERTIES
    OUTPUT_NAME "rflow_svc"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
)

target_include_directories(rflow_svc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/service
        ${CMAKE_SOURCE_DIR}/src/service/impl
)

target_link_libraries(rflow_svc PRIVATE rflow_sdk_internal_deps)

if(RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    target_include_directories(rflow_svc SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/include/webrtc
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/abseil-cpp
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/libyuv/include
    )
    target_link_libraries(rflow_svc PRIVATE rflow_webrtc_prebuilt)
    target_compile_definitions(rflow_svc PRIVATE
        RFLOW_SVC_WEBRTC_IMPL=1
        RFLOW_RTC_WEBRTC_PEER_CONNECTION_API=1
        WEBRTC_POSIX=1
    )
    if(ANDROID)
        target_compile_definitions(rflow_svc PRIVATE WEBRTC_ANDROID=1)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(rflow_svc PRIVATE WEBRTC_LINUX=1)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RFLOW_LINUX_GIO IMPORTED_TARGET glib-2.0 gobject-2.0 gio-2.0)
            if(TARGET PkgConfig::RFLOW_LINUX_GIO)
                target_link_libraries(rflow_svc PRIVATE PkgConfig::RFLOW_LINUX_GIO)
            endif()
        endif()
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        target_compile_definitions(rflow_svc PRIVATE RFLOW_HAVE_ROCKCHIP_MPP=1)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RFLOW_SVC_MPP IMPORTED_TARGET rockchip_mpp)
            if(TARGET PkgConfig::RFLOW_SVC_MPP)
                target_link_libraries(rflow_svc PRIVATE PkgConfig::RFLOW_SVC_MPP)
            endif()
        endif()
        if(NOT TARGET PkgConfig::RFLOW_SVC_MPP)
            find_library(RFLOW_SVC_MPP_LIB NAMES rockchip_mpp mpp
                PATHS /usr/lib/aarch64-linux-gnu /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)
            if(RFLOW_SVC_MPP_LIB)
                target_link_libraries(rflow_svc PRIVATE ${RFLOW_SVC_MPP_LIB})
            endif()
        endif()
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    target_compile_options(rflow_svc PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()
if(RFLOW_SHARED_VERSION_SCRIPT)
    target_link_options(rflow_svc PRIVATE
        "-Wl,--version-script=${RFLOW_SHARED_VERSION_SCRIPT}")
endif()
