add_library(imprint_utest INTERFACE)
# SYSTEM keeps the vendored header's own code out of our warnings.
target_include_directories(imprint_utest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/utest)
add_library(imprint_tomlc17 STATIC tomlc17/tomlc17.c)
target_include_directories(imprint_tomlc17 SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tomlc17)
# Vendored code is compiled as upstream ships it, which may rely on GNU or POSIX extensions.
set_target_properties(imprint_tomlc17 PROPERTIES C_EXTENSIONS ON)
add_library(imprint_cjson STATIC cjson/cJSON.c)
target_include_directories(imprint_cjson SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cjson)
set_target_properties(imprint_cjson PROPERTIES C_EXTENSIONS ON)
if(NOT WIN32)
    target_link_libraries(imprint_cjson PRIVATE m)
endif()
