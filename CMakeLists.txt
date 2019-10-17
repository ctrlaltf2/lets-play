cmake_minimum_required(VERSION 3.2 FATAL_ERROR)
include("cmake/HunterGate.cmake")
HunterGate(
        URL "https://github.com/cpp-pm/hunter/archive/v0.23.219.tar.gz"
        SHA1 "9cf34f61c8d34cfc2b1396a63aaaedaa5e09441e"
        LOCAL
)
project(letsplay
    DESCRIPTION "Emulator thing"
)

set(Boost_USE_STATIC_LIBS OFF)
set(BUILD_SHARED_LIBS ON)
set(Boost_NO_BOOST_CMAKE OFF)

hunter_add_package(Boost COMPONENTS system filesystem program_options)
hunter_add_package(websocketpp)
hunter_add_package(libjpeg-turbo)

find_package(Boost CONFIG REQUIRED system filesystem program_options)
find_package(websocketpp CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)

find_package(Threads)
find_package(JPEG REQUIRED)

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Updating submodules...")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule foreach git fetch origin master
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(WARNING "git submodule foreach git fetch origin master failed with ${GIT_SUBMOD_RESULT}. Please manually checkout submodules.")
        endif()
    endif()
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
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
        )

if(NOT WIN32)
    # TODO(modeco80): Figure out a way to make this work on the Windows because microsoft bad
    add_custom_target(update-client ALL
            DEPENDS
            ${CMAKE_SOURCE_DIR}/bin/client
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/client
            COMMAND git pull || (true && echo Local client change overriding remote change...) # Having this will prioritize local changes over remote ones
            COMMENT Updating client...
            )
    add_custom_command(
            OUTPUT
            ${CMAKE_SOURCE_DIR}/bin/client
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
            COMMAND git clone https://github.com/ctrlaltf2/lets-play-client client
            COMMENT Fetching client..
            )

    add_dependencies(letsplay update-client)
endif()		

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
        external/json/single_include
)

if(UNIX)
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
endif()

if(UNIX)
    target_link_libraries(letsplay
        PUBLIC
            ${CMAKE_DL_LIBS}
			${CMAKE_THREAD_LIBS_INIT}
    )
endif()

if(WIN32)
    target_link_libraries(letsplay
        PUBLIC
            bcrypt
    )
endif()

target_link_libraries(letsplay
    PUBLIC
        Boost::boost
        Boost::system
		Boost::program_options
        Boost::filesystem
        websocketpp::websocketpp
        libjpeg-turbo::turbojpeg-static
)