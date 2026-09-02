find_package(Threads REQUIRED)

if(VITA)
    set(KWIK_BACKEND_DEFAULT "vitagl")
    set(KWIK_VITASDK "${VITASDK}")
    if(NOT KWIK_VITASDK)
        set(KWIK_VITASDK "$ENV{VITASDK}")
    endif()
    find_program(KWIK_HOST_PKG_CONFIG pkg-config NO_CMAKE_FIND_ROOT_PATH)
    if(KWIK_VITASDK AND KWIK_HOST_PKG_CONFIG)
        set(PKG_CONFIG_EXECUTABLE "${KWIK_HOST_PKG_CONFIG}" CACHE FILEPATH "Path of pkg-config executable" FORCE)
        set(ENV{PKG_CONFIG_DIR} "")
        set(ENV{PKG_CONFIG_PATH} "")
        set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
        set(ENV{PKG_CONFIG_LIBDIR} "${KWIK_VITASDK}/arm-vita-eabi/lib/pkgconfig:${KWIK_VITASDK}/arm-vita-eabi/share/pkgconfig")
    endif()
else()
    set(KWIK_BACKEND_DEFAULT "glfw")
endif()

find_package(PkgConfig QUIET)
set(KWIK_BACKEND "${KWIK_BACKEND_DEFAULT}" CACHE STRING "render backend: glfw or vitagl")

set(KWIK_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)

if(KWIK_BACKEND STREQUAL "vitagl")
    set(KWIK_BACKEND_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/vitagl.cpp)
elseif(KWIK_BACKEND STREQUAL "glfw")
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)
    set(KWIK_BACKEND_SOURCE
        ${CMAKE_CURRENT_SOURCE_DIR}/source/backends/opengl.cpp
        ${KWIK_THIRD_PARTY}/glad/src/glad.c
    )
else()
    message(FATAL_ERROR "Supported backends: glfw, vitagl")
endif()

if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libavutil libswscale libswresample)
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
    ${KWIK_THIRD_PARTY}
)

if(KWIK_BACKEND STREQUAL "vitagl")
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

if(FFMPEG_FOUND)
    target_include_directories(kwik_runtime PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_options(kwik_runtime PRIVATE ${FFMPEG_CFLAGS_OTHER})
    target_compile_definitions(kwik_runtime PRIVATE KWIK_USE_FFMPEG=1)
    target_link_directories(kwik_runtime PUBLIC ${FFMPEG_LIBRARY_DIRS})
    target_link_options(kwik_runtime PUBLIC ${FFMPEG_LDFLAGS_OTHER})
    target_link_libraries(kwik_runtime PUBLIC ${FFMPEG_LIBRARIES})
endif()
