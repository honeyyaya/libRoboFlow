# src/common — Client / Service 共享实现
# 生成对象库，由 client / service 目标各自链接
add_library(rflow_common OBJECT
    log/last_error.cpp
    log/logger.cpp
    base/memory.cpp
    base/version.cpp

    abi/log_config.cpp
    abi/signal_config.cpp
    abi/license_config.cpp
    abi/global_config.cpp

    media/video_frame.cpp
    media/stream_stats.cpp
)

target_include_directories(rflow_common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL OR RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    target_include_directories(rflow_common SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/include/webrtc
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/abseil-cpp
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/libyuv/include
    )
endif()

target_link_libraries(rflow_common PRIVATE rflow_warnings)

# 把 git/build 时间戳注入 version.cpp 编译单元（不污染 public header librflow_version.h；
# 如未提供则该 .cpp 通过 librflow_version.h 内的 #ifndef fallback 用 "unknown"）。
set_source_files_properties(base/version.cpp PROPERTIES
    COMPILE_DEFINITIONS
        "COMMIT_VERSION=\"${RFLOW_GIT_COMMIT}\";COMMIT_TIME=\"${RFLOW_GIT_COMMIT_TIME}\";BUILD_TIME=\"${RFLOW_BUILD_TIME}\"")
