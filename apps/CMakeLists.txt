# apps/ — 独立可执行入口，不打进 librflow_client / librflow_svc。
# 默认仅构建 signaling_server（无 webrtc 依赖）；
# push_demo / pull_demo / mpp_h264_smoke 依赖 libwebrtc + SDL + Rockchip MPP，
# 须显式打开 RFLOW_SERVICE_ENABLE_WEBRTC_IMPL 并完成 service/impl 集成后再启用。

# --- signaling_server (TCP + epoll, 纯 POSIX) -----------------------------------
add_executable(signaling_server signaling_server.cpp
    $<TARGET_OBJECTS:rflow_core>
    $<TARGET_OBJECTS:rflow_signal_server>
)
target_include_directories(signaling_server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(signaling_server PRIVATE rflow_warnings)
find_package(Threads REQUIRED)
target_link_libraries(signaling_server PRIVATE Threads::Threads)

# --- webrtc demo 执行件（预留，默认不构建）-------------------------------------
if(RFLOW_SERVICE_ENABLE_WEBRTC_IMPL AND TARGET rflow_svc_impl)
    foreach(_app push_demo pull_demo mpp_h264_smoke)
        add_executable(${_app} ${_app}.cpp)
        target_include_directories(${_app} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/service/impl
        )
        target_link_libraries(${_app} PRIVATE
            rflow_warnings
            rflow_svc_impl
            rflow_webrtc_prebuilt
            Threads::Threads
            ${CMAKE_DL_LIBS}
        )
    endforeach()
else()
    message(STATUS "apps: push_demo/pull_demo/mpp_h264_smoke 未启用 "
                   "(需 RFLOW_SERVICE_ENABLE_WEBRTC_IMPL 且 service/impl 已接入)")
endif()
