cmake_minimum_required(VERSION 3.16)
project(kwik C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(compiler)
add_subdirectory(runtime)

set(KWIK_GAME "$ENV{HOME}/GameMakerProjects/kwiktests/build/squashfs-root/usr/bin/assets/game.unx"
    CACHE FILEPATH "GameMaker data file to recompile")

set(KWIK_GENERATED ${CMAKE_BINARY_DIR}/generated.cpp)

add_custom_command(
    OUTPUT ${KWIK_GENERATED}
    COMMAND kwikc ${KWIK_GAME} --emit ${KWIK_GENERATED}
    DEPENDS kwikc ${KWIK_GAME}
    COMMENT "Recompiling GML -> C++ from ${KWIK_GAME}"
    VERBATIM
)

add_executable(game ${KWIK_GENERATED})
target_link_libraries(game PRIVATE kwik_runtime)
