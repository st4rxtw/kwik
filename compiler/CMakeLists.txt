add_executable(kwikc
    source/main.cpp
    source/iff.cpp
    source/disasm.cpp
    source/lift.cpp
)
target_include_directories(kwikc PRIVATE include)
