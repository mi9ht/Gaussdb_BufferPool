cmake_minimum_required(VERSION 3.10)
project(GaussDB_BufferPool LANGUAGES CXX)

# ==============================
# 基础设置
# ==============================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================
# 目录与头文件路径
# ==============================
include_directories(include)

# 源文件自动收集
file(GLOB_RECURSE SOURCES
    src/*.cpp
)

# ==============================
# 构建主程序
# ==============================
add_executable(example example.cpp ${SOURCES})

# POSIX 线程库（Linux 下必须）
target_link_libraries(example pthread)

# ==============================
# 输出路径设置
# ==============================
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

message(STATUS "✅ Build configuration ready:")
message(STATUS "    C++ Standard: C++17")
message(STATUS "    Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "    Output dir: ${EXECUTABLE_OUTPUT_PATH}")
