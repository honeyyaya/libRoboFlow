# apps/ — libRoboFlow 自带的 demo / 信令服务可执行文件。
#
# 构建类型与顶层 CMakeLists.txt 一致（CMAKE_BUILD_TYPE=Debug|Release），
# SDK 库（rflow_client / rflow_svc）与 demo 可执行文件共用同一配置。
#
# 三个目标：
#   - signaling_server : 始终构建。纯 POSIX TCP/epoll，无 webrtc 依赖。
#   - push_demo_sdk    : 只用 librflow_svc 公共 ABI 的推流 demo。
#   - pull_demo_sdk    : 只用 librflow_client 公共 ABI 的拉流 demo。
#
# common/ 为 demo 私有 helper，不进 SDK：
#   demo_helpers.*          信号、状态日志、相机路径
#   demo_camera_hotplug.*   push：Linux USB 相机 udev（仅 push 链接）
#   demo_push_session.*     push：信令 + 推流会话与热插拔调度

find_package(Threads REQUIRED)

# Debug 时给 demo 打 RFLOW_APPS_DEBUG，并保留栈帧便于 gdb。
function(_rflow_apps_apply_build_type target)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(${target} PRIVATE RFLOW_APPS_DEBUG=1)
        if(NOT MSVC)
            target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)
        endif()
    endif()
endfunction()

message(STATUS "apps: CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------- signaling_server
add_executable(signaling_server signaling_server.cpp common/demo_privilege.cpp
    $<TARGET_OBJECTS:rflow_common>
    $<TARGET_OBJECTS:rflow_core>
    $<TARGET_OBJECTS:rflow_signal_server>
)
target_include_directories(signaling_server     PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/common
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(signaling_server PRIVATE rflow_warnings Threads::Threads)
_rflow_apps_apply_build_type(signaling_server)

# ---------------------------------------------------------------- SDK demos
function(_rflow_apps_add_sdk_demo target source link_lib)
    add_executable(${target} ${source} common/demo_helpers.cpp common/demo_privilege.cpp)
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(${target} PRIVATE
        rflow_warnings ${link_lib} Threads::Threads ${CMAKE_DL_LIBS})
    _rflow_apps_apply_build_type(${target})
endfunction()

if(RFLOW_BUILD_SERVICE AND RFLOW_SERVICE_ENABLE_WEBRTC_IMPL)
    add_executable(push_demo_sdk
        push_demo_sdk.cpp
        common/demo_helpers.cpp
        common/demo_privilege.cpp
        common/demo_v4l_linux.cpp
        common/demo_camera_hotplug.cpp
        common/demo_push_session.cpp
    )
    target_compile_definitions(push_demo_sdk PRIVATE RFLOW_APPS_PUSH_DEMO=1)
    target_include_directories(push_demo_sdk PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(push_demo_sdk PRIVATE
        rflow_warnings rflow_svc Threads::Threads ${CMAKE_DL_LIBS})

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBUDEV libudev)
        endif()
        if(NOT LIBUDEV_FOUND)
            find_library(LIBUDEV_LIBRARY NAMES udev)
            if(LIBUDEV_LIBRARY)
                set(LIBUDEV_LIBRARIES ${LIBUDEV_LIBRARY})
                set(LIBUDEV_FOUND TRUE)
            endif()
        endif()
        if(LIBUDEV_FOUND)
            target_link_libraries(push_demo_sdk PRIVATE ${LIBUDEV_LIBRARIES})
            if(LIBUDEV_INCLUDE_DIRS)
                target_include_directories(push_demo_sdk PRIVATE ${LIBUDEV_INCLUDE_DIRS})
            endif()
        else()
            message(WARNING "push_demo_sdk: libudev not found; Linux USB hotplug monitor disabled at link time")
        endif()
    endif()
    _rflow_apps_apply_build_type(push_demo_sdk)
endif()

if(RFLOW_BUILD_CLIENT AND RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    _rflow_apps_add_sdk_demo(pull_demo_sdk pull_demo_sdk.cpp rflow_client)
endif()
