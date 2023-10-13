cmake_minimum_required(VERSION 3.10)
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext/better-cmake/cmake/betterConfig.cmake")
    execute_process(COMMAND git submodule update --init "${CMAKE_CURRENT_SOURCE_DIR}/ext/better-cmake" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}") 
endif()
find_package(better REQUIRED PATHS ext/better-cmake/cmake)

project(shl)
project_author("DaemonTsun")

add_exe(spirv-parser
    SOURCES_DIR "${ROOT}/src/"
    CPP_VERSION 20
    CPP_WARNINGS ALL SANE FATAL
    EXT
        LIB shl 0.8.1 "${ROOT}/ext/shl" INCLUDE LINK GIT_SUBMODULE
    )

