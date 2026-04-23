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

project(libRoboFlow
    VERSION   0.9.4
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(RFLOW_BUILD_SERVICE)
    enable_language(C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

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

if(EXISTS "${CMAKE_SOURCE_DIR}/test/CMakeLists.txt")
    add_subdirectory(test)
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
message(STATUS "  Apps       : ${RFLOW_BUILD_APPS}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Target     : ${CMAKE_SYSTEM_PROCESSOR} / ${CMAKE_SYSTEM_NAME}")
message(STATUS "")
