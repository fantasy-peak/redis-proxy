
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
    trantor
    GIT_REPOSITORY https://github.com/an-tao/trantor.git
    GIT_TAG master
    SOURCE_DIR ${CMAKE_BINARY_DIR}/third_party/trantor
)
FetchContent_MakeAvailable(trantor)

add_executable(redis-proxy
    src/main.cpp
    src/redis_proxy.cpp
)

target_include_directories(redis-proxy PRIVATE include ${CMAKE_SOURCE_DIR}/include;
    ${CMAKE_BINARY_DIR}/third_party/trantor;${CMAKE_BINARY_DIR}/third_party/yaml-cpp/include;)

target_link_libraries(redis-proxy 
    trantor
    yaml-cpp
)
