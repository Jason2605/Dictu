cmake_minimum_required(VERSION 3.16.3)
project(dictu_api C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(DISABLE_HTTP OFF CACHE BOOL "Determines if HTTPS based features are compiled. HTTPS based features require cURL.")

option(BUILD_CLI "Build the CLI" ON)

add_subdirectory(src)

if (BUILD_CLI)
    add_subdirectory(src/cli)
endif()