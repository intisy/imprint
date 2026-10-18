add_library(imprint_utest INTERFACE)
# SYSTEM keeps the vendored header's own code out of our warnings.
target_include_directories(imprint_utest SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/utest)
