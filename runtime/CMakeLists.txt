add_library(kwik_runtime STATIC
    source/builtins.cpp
    source/game.cpp
)
target_include_directories(kwik_runtime PUBLIC include)
