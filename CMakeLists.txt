cmake_minimum_required(VERSION 3.10)
project(GaussDB_BufferPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

include_directories(include)

file(GLOB SOURCES
    src/*.cpp
)

add_executable(example example.cpp ${SOURCES})
target_link_libraries(example pthread)
