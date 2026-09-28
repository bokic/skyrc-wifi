cmake_minimum_required(VERSION 3.15)

project(libskyrc C)

set(LIBSKYRC_VERSION "" CACHE STRING "Override the library version (X.Y.Z)")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

find_package(json-c CONFIG)

find_package(Git QUIET)

set(LIBSKYRC_GIT_TAG "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git" AND Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --abbrev=0
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE LIBSKYRC_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(LIBSKYRC_VERSION STREQUAL "" AND DEFINED ENV{LIBSKYRC_VERSION} AND NOT "$ENV{LIBSKYRC_VERSION}" STREQUAL "")
    set(LIBSKYRC_VERSION "$ENV{LIBSKYRC_VERSION}")
endif()
if(LIBSKYRC_VERSION STREQUAL "")
    if(LIBSKYRC_GIT_TAG STREQUAL "")
        message(FATAL_ERROR "Git tag is not set; set LIBSKYRC_VERSION to build without a Git tag")
    endif()
    string(REGEX REPLACE "^v" "" LIBSKYRC_VERSION "${LIBSKYRC_GIT_TAG}")
endif()
if(NOT LIBSKYRC_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "Version '${LIBSKYRC_VERSION}' must be a semantic version (for example, 4.2.0)")
endif()
string(REGEX REPLACE "^([0-9]+\\.[0-9]+)\\.[0-9]+$" "\\1" LIBSKYRC_SOVERSION "${LIBSKYRC_VERSION}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

include_directories(PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(libskyrc-wifi SHARED
    src/skyrc-wifi.c
    include/skyrc-wifi.h
)
target_compile_definitions(libskyrc-wifi PRIVATE libskyrc_wifi_EXPORTS)
target_link_libraries(libskyrc-wifi PRIVATE json-c)

set_target_properties(libskyrc-wifi PROPERTIES
    PUBLIC_HEADER "../include/skyrc-wifi.h"
    INTERPROCEDURAL_OPTIMIZATION TRUE
    C_VISIBILITY_PRESET hidden
    OUTPUT_NAME "skyrc-wifi"
    VERSION "${LIBSKYRC_VERSION}"
    SOVERSION "${LIBSKYRC_SOVERSION}"
)
