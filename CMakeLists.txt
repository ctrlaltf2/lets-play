cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(letsplay
    LANGUAGES CXX
    DESCRIPTION "Emulator thing"
)

add_executable(letsplay
    # src/
        src/Main.cpp
        src/Server.cpp
        src/Scheduler.cpp
        # Emulator/
            src/Emulator/EmulatorController.cpp
            src/Emulator/RetroCore.cpp
)

set_target_properties(letsplay
    PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY bin
)

target_compile_features(letsplay
    PUBLIC
        cxx_std_17
)

target_include_directories(letsplay
    PUBLIC
        include
)

target_compile_options(letsplay
    PUBLIC
        -Wall
        -Wextra
        -O3
        -g
        -ggdb
        -pedantic-errors
)

target_link_libraries(letsplay
    PUBLIC
        dl
        pthread
        boost_system
)
