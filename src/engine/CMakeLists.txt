add_library(imprint_engine STATIC engine.c plan.c)
target_include_directories(imprint_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(imprint_engine PUBLIC imprint_core PRIVATE imprint_warnings)

add_executable(imprint-engine main.c)
target_link_libraries(imprint-engine PRIVATE imprint_engine imprint_warnings)
