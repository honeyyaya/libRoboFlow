# src/common — Client / Service 共享实现
# 生成对象库，由 client / service 目标各自链接
add_library(rflow_common OBJECT
    error/last_error.cpp

    log/log_config.cpp
    log/logger.cpp

    config/signal_config.cpp
    config/license_config.cpp
    config/global_config.cpp

    frame/video_frame.cpp
    frame/stream_stats.cpp

    util/memory.cpp
    util/version.cpp
)

target_include_directories(rflow_common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rflow_common PRIVATE rflow_warnings)
