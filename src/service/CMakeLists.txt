set(SERVICE_SOURCES
    connect_info.cpp
    connect_cb.cpp
    stream_param.cpp
    stream_cb.cpp
    push_frame.cpp
    license_info.cpp
    lifecycle.cpp
    stream.cpp
    signaling.cpp
    runtime_log.cpp
)

# -------------------------------------------------------------------- impl 层
# WebRTC 业务实现（push_streamer / signaling_client / publisher 等）仅在显式打开
# RFLOW_SERVICE_ENABLE_WEBRTC_IMPL 时编译并链入 rflow_svc。未打开时 rflow_svc
# 保持"纯骨架"，push_video_frame 等返回 OK，不搬运媒体数据。
add_subdirectory(impl)

if(RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_rtc_webrtc>)
    set(_SVC_IMPL_OBJS $<TARGET_OBJECTS:rflow_svc_impl>)
else()
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_rtc_stub>)
    set(_SVC_IMPL_OBJS "")
endif()

if(RFLOW_BUILD_SHARED)
    add_library(rflow_svc SHARED ${SERVICE_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        ${_RRTC_OBJS}
        ${_SVC_IMPL_OBJS}
    )
else()
    add_library(rflow_svc STATIC ${SERVICE_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
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
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/impl
)

target_link_libraries(rflow_svc PRIVATE rflow_warnings)

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
        target_link_libraries(rflow_svc PRIVATE mediandk android log OpenSLES EGL GLESv2)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(rflow_svc PRIVATE WEBRTC_LINUX=1)
        find_package(Threads REQUIRED)
        target_link_libraries(rflow_svc PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RFLOW_LINUX_GIO IMPORTED_TARGET glib-2.0 gobject-2.0 gio-2.0)
            if(TARGET PkgConfig::RFLOW_LINUX_GIO)
                target_link_libraries(rflow_svc PRIVATE PkgConfig::RFLOW_LINUX_GIO)
            endif()
        endif()
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        target_compile_definitions(rflow_svc PRIVATE WEBRTC_DEMO_HAVE_ROCKCHIP_MPP=1)
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    target_compile_options(rflow_svc PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()
# TODO: 后续通过 version script (GNU) 精准导出公开符号
