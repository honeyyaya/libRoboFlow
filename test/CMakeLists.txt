# test/CMakeLists.txt — libRoboFlow 单元测试
#
# 设计原则：
#   - 只测 base / abi / core 中的纯逻辑工具，不依赖 libwebrtc，编译快、跑得快；
#   - 每个 .cpp 一个测试目标，便于按域跑 (ctest -R) 和并行；
#   - 所测源直接编进单测目标（不复用 rflow_core/rflow_svc 的 OBJECT 库），
#     避免被 service WebRTC 链接关系绑死。
#
# 启用：cmake -DRFLOW_BUILD_TESTS=ON ...
# 跑：  cd build && ctest --output-on-failure

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 全局 include：单测源码使用与 SDK 一致的 "base/..." / "rtc/..." 前缀（分别以 src/common、src/core 为根）。
set(_RFLOW_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/core
)

# 单测公共编译标记
set(_RFLOW_TEST_COMPILE_OPTS -Wall -Wextra -Wno-unused-parameter)

# add_rflow_unit_test(<name> SOURCES src1.cpp [src2.cpp ...])
#   - 创建 ${name} 可执行，链接 GTest::GTest GTest::Main + pthread
#   - 自动 add_test，命名为 unit.${name}
function(add_rflow_unit_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_rflow_unit_test(${name}): missing SOURCES")
    endif()
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${_RFLOW_TEST_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${_RFLOW_TEST_COMPILE_OPTS})
    target_link_libraries(${name} PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME unit.${name} COMMAND ${name})
endfunction()

# -------------------------------------------------------------------- common/base
add_rflow_unit_test(test_abi_string_copy
    SOURCES unit/test_abi_string_copy.cpp)

add_rflow_unit_test(test_env_reader
    SOURCES unit/test_env_reader.cpp)

add_rflow_unit_test(test_lifecycle_fsm
    SOURCES unit/test_lifecycle_fsm.cpp)

# -------------------------------------------------------------------- common/abi
add_rflow_unit_test(test_object_access
    SOURCES unit/test_object_access.cpp)

# -------------------------------------------------------------------- core/signal
# protocol.cpp 是纯 std 实现，直接编进单测目标即可（避免拽 rflow_core OBJECT lib）
add_rflow_unit_test(test_signal_protocol
    SOURCES
        unit/test_signal_protocol.cpp
        ${CMAKE_SOURCE_DIR}/src/core/signal/protocol.cpp)

# -------------------------------------------------------------------- core/rtc
add_rflow_unit_test(test_pending_ice_buffer
    SOURCES unit/test_pending_ice_buffer.cpp)

# -------------------------------------------------------------------- service/impl
# subscriber_offer_pump.cpp 现在依赖 rflow::thread::post，需要把 thread_pool.cpp +
# logger.cpp（thread_pool 内部 RFLOW_CORE_LOGI → ::rflow::logf）一起编入。
add_rflow_unit_test(test_subscriber_offer_pump
    SOURCES
        unit/test_subscriber_offer_pump.cpp
        ${CMAKE_SOURCE_DIR}/src/service/impl/subscriber_offer_pump.cpp
        ${CMAKE_SOURCE_DIR}/src/core/thread/thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/common/base/logger.cpp)
target_include_directories(test_subscriber_offer_pump PRIVATE
    ${CMAKE_SOURCE_DIR}/src/service/impl)

# -------------------------------------------------------------------- core/runtime
# runtime_knobs.cpp 依赖 RFLOW_CORE_LOGI → ::rflow::logf，必须把 logger.cpp 一起编进来。
add_rflow_unit_test(test_runtime_knobs
    SOURCES
        unit/test_runtime_knobs.cpp
        ${CMAKE_SOURCE_DIR}/src/core/runtime/runtime_knobs.cpp
        ${CMAKE_SOURCE_DIR}/src/common/base/logger.cpp)

# service：stream_param 常用 ABI + ResolvePublisherStartup（不显式启用 RFLOW_SVC_WEBRTC_IMPL，避免 Publisher）
add_rflow_unit_test(test_svc_stream_param_knobs
    SOURCES
        unit/test_svc_stream_param_knobs.cpp
        ${CMAKE_SOURCE_DIR}/src/service/api_stream_objects.cpp
        ${CMAKE_SOURCE_DIR}/src/service/internal/stream_startup_policy.cpp
        ${CMAKE_SOURCE_DIR}/src/service/internal/service_default_params.cpp
        ${CMAKE_SOURCE_DIR}/src/core/runtime/runtime_knobs.cpp
        ${CMAKE_SOURCE_DIR}/src/common/base/logger.cpp)
target_include_directories(test_svc_stream_param_knobs PRIVATE ${CMAKE_SOURCE_DIR}/src/service)

# -------------------------------------------------------------------- common/public
# log_tagged.h 是 RFLOW_LOG_TAG_* 宏；测试需 logger.cpp 提供 ::rflow::logf 实现。
add_rflow_unit_test(test_log_tagged
    SOURCES
        unit/test_log_tagged.cpp
        ${CMAKE_SOURCE_DIR}/src/common/base/logger.cpp)

# -------------------------------------------------------------------- service/impl/media
# zero_copy_pipeline_policy.cpp 是纯 std + std::getenv，可独立编入。
add_rflow_unit_test(test_zero_copy_pipeline_policy
    SOURCES
        unit/test_zero_copy_pipeline_policy.cpp
        ${CMAKE_SOURCE_DIR}/src/service/impl/media/zero_copy_pipeline_policy.cpp)
target_include_directories(test_zero_copy_pipeline_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/service/impl)

# -------------------------------------------------------------------- common/base
# last_error.cpp 是纯 std；验证内部 set_last_error_* 重载、thread-local 隔离，
# 及对外 ABI librflow_get_last_error；origin / timestamp_ms 走内部 snapshot，未对外。
add_rflow_unit_test(test_last_error
    SOURCES
        unit/test_last_error.cpp
        ${CMAKE_SOURCE_DIR}/src/common/base/last_error.cpp)

# -------------------------------------------------------------------- core/thread
# thread_pool.cpp 依赖 RFLOW_CORE_LOGI → ::rflow::logf；与 runtime_knobs 同款做法。
add_rflow_unit_test(test_thread_pool
    SOURCES
        unit/test_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/core/thread/thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/common/base/logger.cpp)

# -------------------------------------------------------------------- ABI checks (Python, no linkage)
add_test(
    NAME unit.check_stream_stats_abi
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_stream_stats_abi.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(unit.check_stream_stats_abi PROPERTIES LABELS abi)

message(STATUS "libRoboFlow unit tests enabled (GTest ${GTEST_VERSION})")
