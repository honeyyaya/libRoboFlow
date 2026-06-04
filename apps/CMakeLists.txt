# apps/ — libRoboFlow 自带的 demo / 信令服务可执行文件。
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
    add_executable(push_demo_sdk
        push_demo_sdk.cpp
        common/demo_helpers.cpp
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
endif()

if(RFLOW_BUILD_CLIENT AND RFLOW_CLIENT_ENABLE_WEBRTC_IMPL)
    _rflow_apps_add_sdk_demo(pull_demo_sdk pull_demo_sdk.cpp rflow_client)
endif()
