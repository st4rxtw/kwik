find_package(Threads REQUIRED)

set(KWIK_BACKEND "glfw" CACHE STRING "render backend: glfw or sdl2")

set(KWIK_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

if(KWIK_BACKEND STREQUAL "sdl2")
    find_package(SDL2 REQUIRED)
    set(KWIK_BACKEND_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/sdl2.cpp)
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)
    set(KWIK_BACKEND_SOURCE
        ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/opengl.cpp
        ${KWIK_THIRD_PARTY}/glad/src/glad.c
    )
endif()

add_library(kwik_runtime STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/value.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_misc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_ch2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_ch5.cpp
    ${KWIK_BACKEND_SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/source/assets_rt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio.cpp
    ${KWIK_THIRD_PARTY}/stb/stb_vorbis.c
)

set_target_properties(kwik_runtime PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

target_include_directories(kwik_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kwik_runtime PRIVATE
    ${KWIK_THIRD_PARTY}/stb
    ${KWIK_THIRD_PARTY}/miniaudio
)

if(KWIK_BACKEND STREQUAL "sdl2")
    if(TARGET SDL2::SDL2)
        set(KWIK_WINDOW_LIBS SDL2::SDL2)
    else()
        target_include_directories(kwik_runtime PRIVATE ${SDL2_INCLUDE_DIRS})
        set(KWIK_WINDOW_LIBS ${SDL2_LIBRARIES})
    endif()
else()
    target_include_directories(kwik_runtime PRIVATE ${KWIK_THIRD_PARTY}/glad/include)
    set(KWIK_WINDOW_LIBS glfw OpenGL::GL)
endif()

if(WIN32)
    target_link_libraries(kwik_runtime PUBLIC ${KWIK_WINDOW_LIBS} ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_definitions(kwik_runtime PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(kwik_runtime PUBLIC ${KWIK_WINDOW_LIBS} ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
