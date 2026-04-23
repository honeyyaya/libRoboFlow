set(SERVICE_SOURCES
    connect_info.cpp
    connect_cb.cpp
    stream_param.cpp
    stream_cb.cpp
    push_frame.cpp
    license_info.cpp
    lifecycle.cpp
    stream.cpp
    signaling.cpp
    runtime_log.cpp
)

if(RFLOW_BUILD_SHARED)
    add_library(rflow_svc SHARED ${SERVICE_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        $<TARGET_OBJECTS:rflow_rtc_stub>
    )
else()
    add_library(rflow_svc STATIC ${SERVICE_SOURCES}
        $<TARGET_OBJECTS:rflow_common>
        $<TARGET_OBJECTS:rflow_core>
        $<TARGET_OBJECTS:rflow_rtc_stub>
    )
endif()

set_target_properties(rflow_svc PROPERTIES
    OUTPUT_NAME "rflow_svc"
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
)

target_include_directories(rflow_svc
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(rflow_svc PRIVATE rflow_warnings)
# TODO: 后续通过 version script (GNU) 精准导出公开符号
