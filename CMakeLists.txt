cmake_minimum_required(VERSION 3.12)
project(kwik C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT VITA)
    add_subdirectory(compiler)
endif()
add_subdirectory(runtime)
