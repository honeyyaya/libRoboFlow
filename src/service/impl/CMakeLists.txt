# src/service/impl — 服务端 WebRTC 业务实现（推/拉流、Camera、信令客户端等）。
#
# 当前状态：代码已从 legacy sdk/ include/ 扁平化到 camera/ media/ signaling/，
# 使用 rflow::service::impl / rflow::rtc 命名空间，并直接依赖 libwebrtc 预编译产物。暂不接入
# librflow_svc 的主目标（否则会把 libwebrtc 全量拽入 service SDK）。
#
# 启用方式：
#   -DRFLOW_SERVICE_ENABLE_WEBRTC_IMPL=ON  （打开 libwebrtc 依赖）
#   -DRFLOW_ENABLE_ROCKCHIP_MPP=ON         （可选；启用 MPP H264/MJPEG 编解码）
#
# 对外产物：
#   rflow_svc_impl   OBJECT 库 — 供 apps/push_demo / apps/pull_demo 等链接。

if(NOT (RFLOW_SERVICE_ENABLE_WEBRTC_IMPL AND RFLOW_BUILD_SERVICE))
    return()
endif()

set(_SVC_IMPL_SRCS
    camera/camera_utils.cpp
    media/push/camera_video_track_source.cpp
    media/push/external_push_video_track_source.cpp
    media/rtc/peer_connection_utils.cpp
    media/pull/pull_subscriber.cpp
    media/pull/pull_subscriber_internals.cpp
    media/pull/pull_subscriber_video_sink.cpp
    media/rtc/video_rtp_stats_log.cpp
    media/policy/zero_copy_pipeline_policy.cpp
    media/policy/capture_fps_pipeline_policy.cpp
    media/push/push_h264_profile.cpp
    media/push/push_streamer.cpp
    media/push/push_streamer_internals.cpp
    media/push/push_streamer_observers.cpp
    signaling/signaling_client.cpp
    publish/publisher.cpp
    publish/subscriber_offer_pump.cpp
)

add_library(rflow_svc_impl OBJECT ${_SVC_IMPL_SRCS})

target_include_directories(rflow_svc_impl
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/service
        ${CMAKE_SOURCE_DIR}/src/service/impl
)

target_link_libraries(rflow_svc_impl PRIVATE
    rflow_warnings
    rflow_webrtc_prebuilt   # 继承 libwebrtc include 路径
)

target_compile_definitions(rflow_svc_impl PRIVATE
    WEBRTC_POSIX=1
)
if(ANDROID)
    target_compile_definitions(rflow_svc_impl PRIVATE WEBRTC_ANDROID=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(rflow_svc_impl PRIVATE WEBRTC_LINUX=1)
endif()
if(RFLOW_ENABLE_ROCKCHIP_MPP)
    target_compile_definitions(rflow_svc_impl PRIVATE RFLOW_HAVE_ROCKCHIP_MPP=1)
    find_path(RFLOW_SVC_IMPL_RGA_INCLUDE_DIR NAMES rga/im2d.hpp
        PATHS /usr/include /usr/local/include)
    if(RFLOW_SVC_IMPL_RGA_INCLUDE_DIR)
        target_compile_definitions(rflow_svc_impl PRIVATE RFLOW_HAVE_LIBRGA=1)
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(rflow_svc_impl PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()
