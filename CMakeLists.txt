cmake_minimum_required(VERSION 3.16)

# -------------------------------------------------------------------- options
option(RFLOW_BUILD_CLIENT  "Build Client SDK (librflow_client)"  ON)
option(RFLOW_BUILD_SERVICE "Build Service SDK (librflow_svc)"    ON)
option(RFLOW_BUILD_APPS    "Build standalone apps (signaling_server, demos)" OFF)
option(RFLOW_BUILD_SHARED  "Build shared libraries (.so)"        ON)
option(RFLOW_ENABLE_WERROR "Treat warnings as errors"            OFF)
option(RFLOW_CLIENT_ENABLE_WEBRTC_IMPL
    "Enable WebRTC client impl (requires include/webrtc + libwebrtc prebuilt)" OFF)
option(RFLOW_SERVICE_ENABLE_WEBRTC_IMPL
    "Enable WebRTC service impl (push/pull/camera; requires libwebrtc prebuilt)" OFF)
option(RFLOW_ENABLE_ROCKCHIP_MPP
    "Enable Rockchip MPP hw codec backends under core/rtc/hw/rockchip_mpp" OFF)
option(RFLOW_ENABLE_STRUCTURE_CHECK
    "Enable source-layer structure checks (core/common include boundaries)" OFF)
option(RFLOW_BUILD_TESTS
    "Build SDK unit tests (depends on GoogleTest)" OFF)

project(libRoboFlow
    VERSION   0.9.4
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# -------------------------------------------------------------------- version stamp
# 通过 git 抽 short hash + commit 时间，注入到 src/common/base/version.cpp 作为编译期定义。
# 干净 working tree 时打 git short hash；脏树打 "<hash>-dirty"；非 git 树打 "unknown"。
# 全部经过 string(STRIP) 处理，避免末尾换行进入预处理器报错。
find_package(Git QUIET)
set(RFLOW_GIT_COMMIT     "unknown")
set(RFLOW_GIT_COMMIT_TIME "unknown")
if(Git_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _rflow_git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _rflow_git_dirty
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=iso-strict HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _rflow_git_ctime
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_rflow_git_hash)
        set(RFLOW_GIT_COMMIT "${_rflow_git_hash}")
        if(_rflow_git_dirty)
            set(RFLOW_GIT_COMMIT "${RFLOW_GIT_COMMIT}-dirty")
        endif()
    endif()
    if(_rflow_git_ctime)
        set(RFLOW_GIT_COMMIT_TIME "${_rflow_git_ctime}")
    endif()
endif()
string(TIMESTAMP RFLOW_BUILD_TIME "%Y-%m-%dT%H:%M:%S" UTC)
message(STATUS "rflow version stamp: commit=${RFLOW_GIT_COMMIT} commit_time=${RFLOW_GIT_COMMIT_TIME} build_time=${RFLOW_BUILD_TIME}")

if(RFLOW_BUILD_SERVICE)
    enable_language(C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

# 未指定时默认 Debug；Linux arm64 会链接 dependences/.../debug/libwebrtc.a（须与业务同为 Debug）。
# 发布/联调性能请 -DCMAKE_BUILD_TYPE=Release（链接 release/libwebrtc.a）。
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

# -------------------------------------------------------------------- warnings (GCC / Clang only, not Windows/MSVC)
if(MSVC)
    message(FATAL_ERROR "libRoboFlow targets Linux/Android (arm64) with GCC or Clang; MSVC is not supported.")
endif()

add_library(rflow_warnings INTERFACE)
target_compile_options(rflow_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
if(RFLOW_ENABLE_WERROR)
    target_compile_options(rflow_warnings INTERFACE -Werror)
endif()

add_library(rflow_sdk_internal_deps INTERFACE)
target_link_libraries(rflow_sdk_internal_deps INTERFACE rflow_warnings)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    target_link_libraries(rflow_sdk_internal_deps INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
elseif(ANDROID)
    target_link_libraries(rflow_sdk_internal_deps INTERFACE mediandk android log OpenSLES EGL GLESv2)
endif()

# GNU ld: export only librflow_* from shared SDK libs (Clang lld accepts --version-script on Linux/Android).
set(RFLOW_SHARED_VERSION_SCRIPT "")
if(RFLOW_BUILD_SHARED AND UNIX AND NOT APPLE)
    set(RFLOW_SHARED_VERSION_SCRIPT "${CMAKE_SOURCE_DIR}/tools/linkmap/librflow_abi.map")
endif()

# -------------------------------------------------------------------- subdirs
add_subdirectory(src/common)
add_subdirectory(src/core)

if(RFLOW_BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

if(RFLOW_BUILD_SERVICE)
    add_subdirectory(src/service)
endif()

if(RFLOW_BUILD_APPS AND EXISTS "${CMAKE_SOURCE_DIR}/apps/CMakeLists.txt")
    add_subdirectory(apps)
endif()

if(RFLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(RFLOW_ENABLE_STRUCTURE_CHECK)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(rflow_structure_check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_structure_rules.py
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_stream_stats_abi.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running libRoboFlow structure + stream_stats ABI checks")
endif()

# -------------------------------------------------------------------- install rules
# 默认 prefix 是 /usr/local；调用方可 -DCMAKE_INSTALL_PREFIX=/path 覆盖。
# 安装两份产物：
#   1) public headers 到 ${CMAKE_INSTALL_PREFIX}/include/rflow/
#   2) librflow_client.so / librflow_svc.so 到 ${CMAKE_INSTALL_PREFIX}/lib/
#   3) cmake config: ${CMAKE_INSTALL_PREFIX}/lib/cmake/libRoboFlow/libRoboFlowConfig.cmake
#      下游可用 find_package(libRoboFlow CONFIG)。
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_RFLOW_INSTALL_TARGETS)
if(RFLOW_BUILD_CLIENT AND TARGET rflow_client)
    list(APPEND _RFLOW_INSTALL_TARGETS rflow_client)
endif()
if(RFLOW_BUILD_SERVICE AND TARGET rflow_svc)
    list(APPEND _RFLOW_INSTALL_TARGETS rflow_svc)
endif()

if(_RFLOW_INSTALL_TARGETS)
    install(TARGETS ${_RFLOW_INSTALL_TARGETS}
        EXPORT  libRoboFlowTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/rflow
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
    )

    install(EXPORT libRoboFlowTargets
        FILE libRoboFlowTargets.cmake
        NAMESPACE rflow::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libRoboFlow)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/libRoboFlowConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)

    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/libRoboFlowConfig.cmake.in
"@PACKAGE_INIT@\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/libRoboFlowTargets.cmake\")\ncheck_required_components(libRoboFlow)\n")
    configure_package_config_file(
        ${CMAKE_CURRENT_BINARY_DIR}/libRoboFlowConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/libRoboFlowConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libRoboFlow)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libRoboFlowConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/libRoboFlowConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libRoboFlow)
endif()

# -------------------------------------------------------------------- summary
message(STATUS "")
message(STATUS "libRoboFlow SDK ${PROJECT_VERSION}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared     : ${RFLOW_BUILD_SHARED}")
message(STATUS "  Client     : ${RFLOW_BUILD_CLIENT}")
message(STATUS "  WebRTC cl. : ${RFLOW_CLIENT_ENABLE_WEBRTC_IMPL}")
message(STATUS "  Service    : ${RFLOW_BUILD_SERVICE}")
message(STATUS "  WebRTC sv. : ${RFLOW_SERVICE_ENABLE_WEBRTC_IMPL}")
message(STATUS "  Rockchip MPP: ${RFLOW_ENABLE_ROCKCHIP_MPP}")
message(STATUS "  Struct check: ${RFLOW_ENABLE_STRUCTURE_CHECK}")
message(STATUS "  Apps       : ${RFLOW_BUILD_APPS}")
message(STATUS "  Unit tests : ${RFLOW_BUILD_TESTS}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Target     : ${CMAKE_SYSTEM_PROCESSOR} / ${CMAKE_SYSTEM_NAME}")
message(STATUS "")
