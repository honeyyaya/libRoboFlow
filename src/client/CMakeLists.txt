set(CLIENT_SOURCES
    api_handles.cpp
    api_stream_objects.cpp
    infrastructure.cpp
    lifecycle.cpp
    stream.cpp
    internal/state_ops.cpp
)

# -------------------------------------------------------------------- impl 层
# 真实拉流、信令在开启 RFLOW_CLIENT_ENABLE_WEBRTC_IMPL 时编译；WebRTC 工厂与 init 在 core/rtc
if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    list(APPEND CLIENT_SOURCES
        impl/rtc_stream/rtc_stream_frame_converter.cpp
        impl/rtc_stream/rtc_stream_session.cpp
        impl/rtc_stream/rtc_stream_manager.cpp
    )
endif()

if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_core_rtc_domain>)
    if(ANDROID)
        list(APPEND _RRTC_OBJS $<TARGET_OBJECTS:rflow_core_platform_android>)
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        list(APPEND _RRTC_OBJS $<TARGET_OBJECTS:rflow_core_platform_rockchip>)
    endif()
else()
    set(_RRTC_OBJS $<TARGET_OBJECTS:rflow_rtc_stub>)
endif()

if(RFLOW_BUILD_SHARED)
    add_library(rflow_client SHARED ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        $<TARGET_OBJECTS:rflow_core_runtime>
        ${_RRTC_OBJS}
    )
else()
    add_library(rflow_client STATIC ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        $<TARGET_OBJECTS:rflow_core_runtime>
        ${_RRTC_OBJS}
    )
endif()

set_target_properties(rflow_client PROPERTIES
    OUTPUT_NAME "rflow_client"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
)

target_include_directories(rflow_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/client
        ${CMAKE_SOURCE_DIR}/src/client/impl
)

# 与预编译 libwebrtc 链接（定义在 src/core/CMakeLists.txt）
if(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    target_include_directories(rflow_client SYSTEM PRIVATE
        ${CMAKE_SOURCE_DIR}/include/webrtc
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/abseil-cpp
        ${CMAKE_SOURCE_DIR}/include/webrtc/third_party/libyuv/include
    )
    target_link_libraries(rflow_client PRIVATE rflow_webrtc_prebuilt)
    target_compile_definitions(rflow_client PRIVATE
        RFLOW_RTC_WEBRTC_PEER_CONNECTION_API=1
        WEBRTC_POSIX=1
    )
    if(ANDROID)
        target_compile_definitions(rflow_client PRIVATE WEBRTC_ANDROID=1)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(rflow_client PRIVATE WEBRTC_LINUX=1)
    endif()
    if(RFLOW_ENABLE_ROCKCHIP_MPP)
        # rflow_core_platform_rockchip 以 OBJECTS 拼入；让 .so 自闭环链接 rockchip_mpp，
        # 避免下游可执行（pull_demo_sdk 等）需重复 -lrockchip_mpp。
        target_compile_definitions(rflow_client PRIVATE RFLOW_HAVE_ROCKCHIP_MPP=1)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RFLOW_CLIENT_MPP IMPORTED_TARGET rockchip_mpp)
            if(TARGET PkgConfig::RFLOW_CLIENT_MPP)
                target_link_libraries(rflow_client PRIVATE PkgConfig::RFLOW_CLIENT_MPP)
            endif()
        endif()
        if(NOT TARGET PkgConfig::RFLOW_CLIENT_MPP)
            find_library(RFLOW_CLIENT_MPP_LIB NAMES rockchip_mpp mpp
                PATHS /usr/lib/aarch64-linux-gnu /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)
            if(RFLOW_CLIENT_MPP_LIB)
                target_link_libraries(rflow_client PRIVATE ${RFLOW_CLIENT_MPP_LIB})
            endif()
        endif()
    endif()
endif()

target_link_libraries(rflow_client PRIVATE rflow_sdk_internal_deps)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    # libwebrtc headers (e.g. PortInterface::Network) need this on GCC 9.
    target_compile_options(rflow_client PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()
if(RFLOW_SHARED_VERSION_SCRIPT)
    target_link_options(rflow_client PRIVATE
        "-Wl,--version-script=${RFLOW_SHARED_VERSION_SCRIPT}")
endif()
