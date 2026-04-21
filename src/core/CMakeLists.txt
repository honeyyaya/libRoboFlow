# src/core — 内部核心：WebRTC 封装 / 信令 / 线程池
# 当前为空壳，留出占位供后续接入 libwebrtc / custom signaling
add_library(robrt_core OBJECT
    rtc/rtc_stub.cpp
    signal/signal_stub.cpp
    thread/thread_pool.cpp
)

target_include_directories(robrt_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(robrt_core PRIVATE robrt_warnings)
