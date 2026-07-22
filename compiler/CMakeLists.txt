add_executable(kwikc
    source/main.cpp
    source/iff.cpp
    source/disasm.cpp
    source/lift.cpp
    source/assets.cpp
)
target_include_directories(kwikc PRIVATE include "${CMAKE_SOURCE_DIR}/third_party/stb")
target_compile_definitions(kwikc PRIVATE KWIK_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

link_directories("C:/Libraries/zlib/lib" "C:/Libraries/bzip2/lib")
find_library(BZ2_LIB bz2 PATHS "C:/Libraries/bzip2/lib" NO_DEFAULT_PATH)
find_library(Z_LIB z PATHS "C:/Libraries/zlib/lib" NO_DEFAULT_PATH)

if(NOT BZ2_LIB)
    find_library(BZ2_LIB bz2)
endif()
if(NOT Z_LIB)
    find_library(Z_LIB z)
endif()

target_link_libraries(kwikc PRIVATE ${BZ2_LIB} ${Z_LIB})
