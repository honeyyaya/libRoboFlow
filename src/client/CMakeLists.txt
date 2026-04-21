set(CLIENT_SOURCES
    connect_info.cpp
    connect_cb.cpp
    stream_param.cpp
    stream_cb.cpp
    lifecycle.cpp
    stream.cpp
    signaling.cpp
    runtime_log.cpp
)

if(ROBRT_BUILD_SHARED)
    add_library(robrt_client SHARED ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:robrt_common>
        $<TARGET_OBJECTS:robrt_core>
    )
else()
    add_library(robrt_client STATIC ${CLIENT_SOURCES}
        $<TARGET_OBJECTS:robrt_common>
        $<TARGET_OBJECTS:robrt_core>
    )
endif()

set_target_properties(robrt_client PROPERTIES
    OUTPUT_NAME "robrt_client"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
)

target_include_directories(robrt_client
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(robrt_client PRIVATE robrt_warnings)
# TODO: 后续通过 version script (GNU) 精准导出公开符号
