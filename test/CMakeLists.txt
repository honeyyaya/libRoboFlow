# 示例程序：仅演示 Client / Service 两端 SDK 初始化（与根 CMake 相同，不支持 MSVC）
if(MSVC)
    return()
endif()

if(ROBRT_BUILD_CLIENT)
    add_executable(demo_client_init demo_client_init.c)
    target_link_libraries(demo_client_init PRIVATE robrt_client robrt_warnings)
    target_include_directories(demo_client_init PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

if(ROBRT_BUILD_SERVICE)
    add_executable(demo_service_init demo_service_init.c)
    target_link_libraries(demo_service_init PRIVATE robrt_svc robrt_warnings)
    target_include_directories(demo_service_init PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()
