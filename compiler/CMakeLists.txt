add_executable(kwikc
    source/main.cpp
    source/iff.cpp
    source/disasm.cpp
    source/lift.cpp
)
target_include_directories(kwikc PRIVATE include)
target_compile_definitions(kwikc PRIVATE KWIK_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
