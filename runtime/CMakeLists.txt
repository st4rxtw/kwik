find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

add_library(kwik_runtime STATIC
    source/builtins.cpp
    source/game.cpp
    source/backends/opengl.cpp
    ${CMAKE_SOURCE_DIR}/third_party/glad/src/glad.c
)

target_include_directories(kwik_runtime PUBLIC include)
target_include_directories(kwik_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/glad/include
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(kwik_runtime PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})
