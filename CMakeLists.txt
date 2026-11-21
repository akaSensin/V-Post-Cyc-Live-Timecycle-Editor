cmake_minimum_required(VERSION 3.20)
project(TimecycleEditor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(src/vendor/minhook)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)
list(FILTER APP_SOURCES EXCLUDE REGEX "src/vendor/")

add_library(TimecycleEditor SHARED ${APP_SOURCES} src/version.rc)

set_target_properties(TimecycleEditor PROPERTIES
    OUTPUT_NAME "TimecycleEditor"
    SUFFIX ".asi"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(TimecycleEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/scripthookv/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/minhook/include"
)

target_link_directories(TimecycleEditor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/scripthookv/lib"
)

target_link_libraries(TimecycleEditor PRIVATE
    minhook
    ScriptHookV
    winhttp
    shell32
)

target_compile_definitions(TimecycleEditor PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(TimecycleEditor PRIVATE /W3 /MP)
endif()
