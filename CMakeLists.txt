cmake_minimum_required(VERSION 3.20)
project(imprint VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(IMPRINT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(IMPRINT_STATIC "Link every executable statically" OFF)
option(IMPRINT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(imprint_warnings INTERFACE)
target_compile_options(imprint_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes)
if(IMPRINT_WARNINGS_AS_ERRORS)
    target_compile_options(imprint_warnings INTERFACE -Werror)
endif()

if(IMPRINT_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(IMPRINT_STATIC)
    add_link_options(-static)
endif()
if(WIN32)
    # MinGW's default printf follows msvcrt, which does not understand %zu or %lld.
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()
add_compile_definitions(IMPRINT_VERSION="${PROJECT_VERSION}")

enable_testing()

add_subdirectory(vendor)
add_subdirectory(src/core)
add_subdirectory(src/engine)
add_subdirectory(src/manager)
add_subdirectory(tests)
