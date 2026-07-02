add_executable(kwikc
    source/main.cpp
    source/iff.cpp
    source/disasm.cpp
    source/lift.cpp
    source/assets.cpp
)
target_include_directories(kwikc PRIVATE include)
target_compile_definitions(kwikc PRIVATE KWIK_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
find_library(BZ2_LIB bz2)
find_library(Z_LIB z)
target_link_libraries(kwikc PRIVATE ${BZ2_LIB} ${Z_LIB})
