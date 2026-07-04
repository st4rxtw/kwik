cmake_minimum_required(VERSION 3.12)
project(kwik_game C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(KWIK_DIR "@KWIK_DIR@" CACHE PATH "kwik repo root")

add_subdirectory(${KWIK_DIR}/runtime ${CMAKE_BINARY_DIR}/kwik_runtime)

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/objects/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/rooms/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.cpp)

add_executable(game ${GAME_SOURCES})
target_include_directories(game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(game PRIVATE kwik_runtime)

if(MSVC)
    set_target_properties(game PROPERTIES LINK_FLAGS "/STACK:8388608")
endif()