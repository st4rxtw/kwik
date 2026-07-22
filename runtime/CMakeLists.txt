find_package(Threads REQUIRED)

if(VITA)
    set(KWIK_BACKEND_DEFAULT "vitagl")
else()
    set(KWIK_BACKEND_DEFAULT "glfw") # change this to sdl2 if u wanna use sdl2 instead
endif()
set(KWIK_BACKEND "${KWIK_BACKEND_DEFAULT}" CACHE STRING "render backend: glfw, sdl2, or vitagl")

set(KWIK_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

if(KWIK_BACKEND STREQUAL "sdl2")
    find_package(SDL2 REQUIRED)
    set(KWIK_BACKEND_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/sdl2.cpp)
elseif(KWIK_BACKEND STREQUAL "vitagl")
    set(KWIK_BACKEND_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/vitagl.cpp)
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

set_target_properties(kwik_runtime PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

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
elseif(KWIK_BACKEND STREQUAL "vitagl")
    set(KWIK_WINDOW_LIBS
        vitaGL
        SceCommonDialog_stub
        SceGxm_stub
        SceDisplay_stub
        SceAppMgr_stub
        SceCtrl_stub
        SceAudio_stub
        mathneon
        vitashark
        SceShaccCgExt
        taihen_stub
        SceShaccCg_stub
        SceKernelDmacMgr_stub
    )
else()
    target_include_directories(kwik_runtime PRIVATE ${KWIK_THIRD_PARTY}/glad/include)
    set(KWIK_WINDOW_LIBS glfw OpenGL::GL)
endif()

if(WIN32)
    target_link_libraries(kwik_runtime PUBLIC ${KWIK_WINDOW_LIBS} ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_definitions(kwik_runtime PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(VITA)
    target_link_libraries(kwik_runtime PUBLIC ${KWIK_WINDOW_LIBS} Threads::Threads m)
    target_compile_definitions(kwik_runtime PRIVATE _GNU_SOURCE)
else()
    target_link_libraries(kwik_runtime PUBLIC ${KWIK_WINDOW_LIBS} ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
