cmake_minimum_required(VERSION 3.1)
project(collab-rat-server)

set(CMAKE_CXX_FLAGS "-Wall")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY bin)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS} -static-libgcc -static -s")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS} -static-libgcc -static-libstdc++ -static -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -fvisibility=hidden -fvisibility-inlines-hidden -fno-ident")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -ggdb")

if(CMAKE_BUILD_TYPE MATCHES DEBUG)
    message("Debug build")
    add_definitions(-DDEBUG)
endif(CMAKE_BUILD_TYPE MATCHES DEBUG)

include_directories(include lib/websocketpp lib/asio/asio/include lib/winutil/include)
file(GLOB_RECURSE SOURCES "src/*.cpp" "lib/winutil/src/*.cpp")

add_executable(collab-rat-server ${SOURCES})
target_link_libraries(collab-rat-server ws2_32 mswsock pthread ntdll)
