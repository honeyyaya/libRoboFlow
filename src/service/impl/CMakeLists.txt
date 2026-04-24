# src/service/impl — 服务端 WebRTC 业务实现（推/拉流、Camera、信令客户端等）。
#
# 当前状态：代码已从 legacy sdk/ include/ 扁平化到 camera/ config/ media/ signaling/，
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
    config/config_loader.cpp
    media/camera_video_track_source.cpp
    media/external_push_video_track_source.cpp
    media/pull_subscriber.cpp
    media/push_streamer.cpp
    signaling/signaling_io_manager.cpp
    signaling/signaling_client.cpp
    publisher.cpp
)

add_library(rflow_svc_impl OBJECT ${_SVC_IMPL_SRCS})

target_include_directories(rflow_svc_impl
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
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
    target_compile_definitions(rflow_svc_impl PRIVATE WEBRTC_DEMO_HAVE_ROCKCHIP_MPP=1)
endif()
