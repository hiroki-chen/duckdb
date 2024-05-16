cmake_minimum_required(VERSION 2.8.12)
project(example-c++)

set(CMAKE_CXX_STANDARD 11)

include_directories(../../src/include ../../../picachv/picachv-api/c_headers)
# link_directories(../../build/src)

add_executable(example main.cpp)
target_link_libraries(example duckdb)
target_link_directories(example PUBLIC ${PROJECT_SOURCE_DIR}/../../build/debug/src)