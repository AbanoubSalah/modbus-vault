set(PROTO_FILE ${CMAKE_CURRENT_LIST_DIR}/proto/modbus.proto)

set(GEN_SRCS ${CMAKE_CURRENT_BINARY_DIR}/modbus.pb-c.c)
set(GEN_INCS ${CMAKE_CURRENT_BINARY_DIR}/modbus.pb-c.h)

find_program(PROTOC_C_EXECUTABLE protoc-c REQUIRED)

idf_component_register(
    SRCS serializer.c ${GEN_SRCS}
    INCLUDE_DIRS include ${CMAKE_CURRENT_BINARY_DIR}
    REQUIRES protobuf-c blackbox_logger modbus_analyzer mqtt_bridge)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Mark generated files
    set_source_files_properties(${GEN_SRCS} ${GEN_INCS} PROPERTIES GENERATED TRUE)

    # Generate protobuf-c sources
    add_custom_command(
        OUTPUT ${GEN_SRCS} ${GEN_INCS}
        COMMAND ${PROTOC_C_EXECUTABLE}
            --proto_path=${CMAKE_CURRENT_LIST_DIR}/proto
            --c_out=${CMAKE_CURRENT_BINARY_DIR}
            ${PROTO_FILE}
        DEPENDS
            ${PROTO_FILE}
            ${PROTOC_C_EXECUTABLE}
        COMMENT "Generating C protobuf sources"
        VERBATIM
    )

    # Ensure target depends on generated files
    add_custom_target(modbus_proto_gen
        DEPENDS
            ${GEN_SRCS}
            ${GEN_INCS}
    )
    add_dependencies(${COMPONENT_LIB} modbus_proto_gen)

    # Clean generated files
    set_property(DIRECTORY ${COMPONENT_DIR}
        APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${GEN_SRCS} ${GEN_INCS})
endif()
