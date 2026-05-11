# apps/ — libRoboFlow 自带的 demo / 信令服务可执行文件。
#
# 三个目标：
#   - signaling_server : 始终构建。纯 POSIX TCP/epoll，无 webrtc 依赖。
#   - push_demo_sdk    : 只用 librflow_svc 公共 ABI 的推流 demo（合成 I420）。
#   - pull_demo_sdk    : 只用 librflow_client 公共 ABI 的拉流 demo。
#
# common/demo_helpers.{h,cpp} 是上述两个 SDK demo 共享的私有 helper（信号处理、
# 状态打印、相机路径解析），不进 SDK，也不被外部使用。

find_package(Threads REQUIRED)

# ---------------------------------------------------------------- signaling_server
add_executable(signaling_server signaling_server.cpp
    $<TARGET_OBJECTS:rflow_common>
    $<TARGET_OBJECTS:rflow_core>
    $<TARGET_OBJECTS:rflow_signal_server>
)
target_include_directories(signaling_server     PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
)
target_link_libraries(signaling_server PRIVATE rflow_warnings Threads::Threads)

# ---------------------------------------------------------------- SDK demos
function(_rflow_apps_add_sdk_demo target source link_lib)
    add_executable(${target} ${source} common/demo_helpers.cpp)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${target} PRIVATE
        rflow_warnings ${link_lib} Threads::Threads ${CMAKE_DL_LIBS})
endfunction()

if(RFLOW_BUILD_SERVICE AND RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    _rflow_apps_add_sdk_demo(push_demo_sdk push_demo_sdk.cpp rflow_svc)
endif()

if(RFLOW_BUILD_CLIENT AND RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    _rflow_apps_add_sdk_demo(pull_demo_sdk pull_demo_sdk.cpp rflow_client)
endif()
