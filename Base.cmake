cmake_minimum_required(VERSION 3.12)
project(kwik_game C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets.dat")
    add_custom_command(TARGET game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/Assets.dat
                $<TARGET_FILE_DIR:game>/Assets.dat
        COMMENT "kwik: copying Assets.dat next to game binary")
endif()

if(VITA)
    include("${VITASDK}/share/vita.cmake" REQUIRED)

    set(KWIK_VITA_TITLEID "KWIK00001" CACHE STRING "PS Vita title ID (9 chars, e.g. KWIK00001)")
    set(KWIK_VITA_NAME "${CMAKE_PROJECT_NAME}" CACHE STRING "PS Vita LiveArea title")

    vita_create_self(eboot.bin game)

    set(KWIK_VITA_VPK_FILES "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png")
        list(APPEND KWIK_VITA_VPK_FILES FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png" sce_sys/icon0.png)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea")
        list(APPEND KWIK_VITA_VPK_FILES FILE "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea" sce_sys/livearea)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets.dat")
        list(APPEND KWIK_VITA_VPK_FILES FILE "${CMAKE_CURRENT_SOURCE_DIR}/Assets.dat" Assets.dat)
    endif()

    vita_create_vpk(game.vpk ${KWIK_VITA_TITLEID} eboot.bin
        VERSION "01.00"
        NAME ${KWIK_VITA_NAME}
        ${KWIK_VITA_VPK_FILES}
    )
endif()