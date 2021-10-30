
cmake_minimum_required(VERSION 3.5)

project(redis-proxy)

include("FetchContent")

set(CMAKE_CXX_STANDARD 20)

add_compile_options(
    -Wall
    -Wunused-parameter
    -gdwarf-2 -O2
    -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
    -pthread
)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG master
    SOURCE_DIR ${CMAKE_BINARY_DIR}/third_party/yaml-cpp
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG master
)
FetchContent_MakeAvailable(asio)

FetchContent_Declare(
	spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.4.2
)
FetchContent_MakeAvailable(spdlog)

add_executable(redis-proxy
    src/main.cpp
)

set(CMAKE_CXX_FLAGS "-fcoroutines")

target_include_directories(redis-proxy PRIVATE include ${CMAKE_SOURCE_DIR}/include;
    ${CMAKE_BINARY_DIR}/_deps/asio-src/asio/include/;${CMAKE_BINARY_DIR}/third_party/yaml-cpp/include;
    ${CMAKE_BINARY_DIR}/_deps/spdlog-src/include/;)

target_link_libraries(redis-proxy 
    yaml-cpp
    pthread
    spdlog
)