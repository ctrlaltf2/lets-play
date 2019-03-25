cmake_minimum_required(VERSION 3.1 FATAL_ERROR)
project(letsplay
    LANGUAGES CXX
    DESCRIPTION "Emulator thing"
)

option(UseSTLFilesystem "Use std::filesystem instead of boost::filesystem" OFF)

if(NOT UseSTLFilesystem)
    find_package(Boost 1.62 COMPONENTS program_options filesystem REQUIRED)
else()
    find_package(Boost 1.62 COMPONENTS program_options REQUIRED)
endif()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Updating submodules")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}. Please manually checkout submodules.")
        endif()
    endif()
endif()

#[[ clang-tidy segfaults on LetsPlayServer.cpp; too complex?
if(CMAKE_VERSION VERSION_GREATER 3.6)
    # Use clang-tidy if available
    option(CLANG_TIDY_FIX "Perform fixes for Clang-Tidy" OFF)
    find_program(
        CLANG_TIDY_EXE
        NAMES "clang-tidy"
        DOC "Path to the clang-tidy executable"
    )

    if(CLANG_TIDY_EXE)
        if(CLANG_TIDY_FIX)
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}" "-fix" "-checks=-*,cppcoreguidelines-*,clang-analyzer-*,misc-*,modernize-*,performance-*,portability-*")
        else()
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}" "-checks=-*,cppcoreguidelines-*,clang-analyzer-*,misc-*,modernize-*,performance-*,portability-*")
        endif()
    endif()
endif()
]]

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/external/json/README.md")
    message(FATAL_ERROR "The json library submodule was not downloaded. Please manually update submodules and try again.")
endif()

add_executable(letsplay
    # src/
        src/Main.cpp
        src/LetsPlayConfig.cpp
        src/LetsPlayServer.cpp
        src/LetsPlayUser.cpp
        src/LetsPlayProtocol.cpp
        src/md5.cpp
        src/Random.cpp
        src/Scheduler.cpp
        # Emulator/
            src/Emulator/EmulatorController.cpp
            src/Emulator/RetroCore.cpp
            src/Emulator/RetroPad.cpp
        )

set_target_properties(letsplay
    PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY bin
)

target_compile_features(letsplay
    PUBLIC
        cxx_auto_type
        cxx_binary_literals
        cxx_constexpr
        cxx_decltype
        cxx_deleted_functions
        cxx_digit_separators
        cxx_generic_lambdas
        cxx_lambdas
        cxx_nonstatic_member_init
        cxx_nullptr
        cxx_range_for
        cxx_raw_string_literals
        cxx_right_angle_brackets
        cxx_std_14
        cxx_variadic_templates
)

target_include_directories(letsplay
    PUBLIC
        include
        include/common
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

if(UseSTLFilesystem)
    target_link_libraries(letsplay
        PUBLIC
            stdc++fs
    )
else()
    target_compile_definitions(letsplay
        PRIVATE
            USE_BOOST_FILESYSTEM
    )
endif()

if(UNIX)
    target_link_libraries(letsplay
        PUBLIC
            ${CMAKE_DL_LIBS}
    )
endif()

target_link_libraries(letsplay
    PUBLIC
        pthread
        boost_system
        ${Boost_LIBRARIES}
        turbojpeg
        atomic
)
