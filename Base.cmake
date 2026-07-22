cmake_minimum_required(VERSION 3.12)
project(kwik_game C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(KWIK_DIR "@KWIK_DIR@" CACHE PATH "kwik repo root")

if(VITA)
    add_subdirectory(${KWIK_DIR}/runtime ${CMAKE_BINARY_DIR}/kwik_runtime)
else()
    set(KWIK_RUNTIME_LIB "${KWIK_DIR}/build/runtime/libkwik_runtime.a" CACHE FILEPATH
        "Path to a prebuilt libkwik_runtime.a. Build the kwik repo root once \
(cmake -B build -S ${KWIK_DIR} && cmake --build build --target kwik_runtime) to produce \
this, instead of recompiling the runtime from source for every emitted game.")
    if(NOT EXISTS "${KWIK_RUNTIME_LIB}")
        message(FATAL_ERROR "kwik: KWIK_RUNTIME_LIB not found at ${KWIK_RUNTIME_LIB} -- "
            "build the root kwik project first (cmake -B build -S ${KWIK_DIR} && "
            "cmake --build build --target kwik_runtime), or set -DKWIK_RUNTIME_LIB=<path> "
            "to point at an existing libkwik_runtime.a")
    endif()

    find_package(Threads REQUIRED)
    set(KWIK_BACKEND "glfw" CACHE STRING "render backend the prebuilt libkwik_runtime.a was built with: glfw or sdl2")

    add_library(kwik_runtime STATIC IMPORTED)
    set_target_properties(kwik_runtime PROPERTIES IMPORTED_LOCATION "${KWIK_RUNTIME_LIB}")
    target_include_directories(kwik_runtime INTERFACE ${KWIK_DIR}/runtime/include)

    if(KWIK_BACKEND STREQUAL "sdl2")
        find_package(SDL2 REQUIRED)
        if(TARGET SDL2::SDL2)
            set(KWIK_WINDOW_LIBS SDL2::SDL2)
        else()
            target_include_directories(kwik_runtime INTERFACE ${SDL2_INCLUDE_DIRS})
            set(KWIK_WINDOW_LIBS ${SDL2_LIBRARIES})
        endif()
    else()
        find_package(OpenGL REQUIRED)
        find_package(glfw3 REQUIRED)
        set(KWIK_WINDOW_LIBS glfw OpenGL::GL)
    endif()

    if(WIN32)
        target_link_libraries(kwik_runtime INTERFACE ${KWIK_WINDOW_LIBS} ${CMAKE_DL_LIBS} Threads::Threads)
    else()
        target_link_libraries(kwik_runtime INTERFACE ${KWIK_WINDOW_LIBS} ${CMAKE_DL_LIBS} Threads::Threads m)
    endif()
endif()

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
