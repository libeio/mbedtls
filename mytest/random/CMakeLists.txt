
include_directories(${ssl_INCLUDE})
link_directories(${MBEDTLS_LIBRARIES})

aux_source_directory(. SRC_LIST)

set_source_files_properties(${SRC_LIST} PROPERTIES LANGUAGE C)

foreach(src ${SRC_LIST})
    string(REGEX REPLACE "^./" "" src ${src})
    
    set(exe ${src})
    string(REGEX REPLACE ".c$" "" exe ${exe})
    
    add_executable(${exe} ${src})
    target_link_libraries(${exe} mbedcrypto)
endforeach(src)
