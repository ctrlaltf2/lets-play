cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(letsplay
    LANGUAGES CXX
    DESCRIPTION "Emulator thing"
)

find_package(Boost 1.62 COMPONENTS program_options REQUIRED)

add_executable(letsplay
    # src/
        src/Main.cpp
        src/LetsPlayConfig.cpp
        src/LetsPlayServer.cpp
        src/LetsPlayUser.cpp
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
        cxx_auto_type
        cxx_binary_literals
        cxx_constexpr
        cxx_generic_lambdas
        cxx_lambdas
        cxx_nonstatic_member_init
        cxx_nullptr
        cxx_range_for
        cxx_raw_string_literals
        cxx_right_angle_brackets
        cxx_std_17
)

target_include_directories(letsplay
    PUBLIC
        include
        ${Boost_INCLUDE_DIR}
        external/json/single_include
)

target_compile_options(letsplay
    PUBLIC
        -Wall
        -Wextra
        -Werror=return-type
        -O3
        -g
        -ggdb
        -pedantic-errors
        -Wfatal-errors
)

target_link_libraries(letsplay
    PUBLIC
        dl
        pthread
        boost_system
        webp
        ${Boost_LIBRARIES}
        stdc++fs
        png
)
