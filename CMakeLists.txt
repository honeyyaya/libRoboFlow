cmake_minimum_required(VERSION 3.16)

project(robrt
    VERSION   0.9.4
    LANGUAGES C CXX
)

# -------------------------------------------------------------------- options
option(ROBRT_BUILD_CLIENT  "Build Client SDK (librobrt_client)"  ON)
option(ROBRT_BUILD_SERVICE "Build Service SDK (librobrt_svc)"    ON)
option(ROBRT_BUILD_SHARED  "Build shared libraries (.so)"        ON)
option(ROBRT_ENABLE_WERROR "Treat warnings as errors"            OFF)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# -------------------------------------------------------------------- warnings (GCC / Clang only — not Windows/MSVC)
if(MSVC)
    message(FATAL_ERROR "robrt targets Linux/Android (arm64) with GCC or Clang; MSVC is not supported.")
endif()

add_library(robrt_warnings INTERFACE)
target_compile_options(robrt_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
if(ROBRT_ENABLE_WERROR)
    target_compile_options(robrt_warnings INTERFACE -Werror)
endif()

# -------------------------------------------------------------------- subdirs
add_subdirectory(src/common)
add_subdirectory(src/core)

if(ROBRT_BUILD_CLIENT)
    add_subdirectory(src/client)
endif()

if(ROBRT_BUILD_SERVICE)
    add_subdirectory(src/service)
endif()

add_subdirectory(test)

# -------------------------------------------------------------------- summary
message(STATUS "")
message(STATUS "robrt SDK ${PROJECT_VERSION}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Shared     : ${ROBRT_BUILD_SHARED}")
message(STATUS "  Client     : ${ROBRT_BUILD_CLIENT}")
message(STATUS "  Service    : ${ROBRT_BUILD_SERVICE}")
message(STATUS "  Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Target     : ${CMAKE_SYSTEM_PROCESSOR} / ${CMAKE_SYSTEM_NAME}")
message(STATUS "")
