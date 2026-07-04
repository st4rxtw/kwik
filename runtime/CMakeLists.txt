find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Threads REQUIRED)

set(KWIK_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

add_library(kwik_runtime STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/value.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_misc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins_ch2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/opengl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/assets_rt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio.cpp
    ${KWIK_THIRD_PARTY}/glad/src/glad.c
    ${KWIK_THIRD_PARTY}/stb/stb_vorbis.c
)

set_target_properties(kwik_runtime PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

target_include_directories(kwik_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kwik_runtime PRIVATE
    ${KWIK_THIRD_PARTY}/glad/include
    ${KWIK_THIRD_PARTY}/stb
    ${KWIK_THIRD_PARTY}/miniaudio
)

if(WIN32)
    target_link_libraries(kwik_runtime PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS} Threads::Threads)
    target_compile_definitions(kwik_runtime PRIVATE _USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_link_libraries(kwik_runtime PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS} Threads::Threads m)
endif()
