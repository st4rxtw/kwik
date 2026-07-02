find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

set(KWIK_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

add_library(kwik_runtime STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/source/builtins.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/game.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/opengl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/assets_rt.cpp
    ${KWIK_THIRD_PARTY}/glad/src/glad.c
)

target_include_directories(kwik_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(kwik_runtime PRIVATE
    ${KWIK_THIRD_PARTY}/glad/include
    ${KWIK_THIRD_PARTY}/stb
)

target_link_libraries(kwik_runtime PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})
