cmake_minimum_required(VERSION 3.15)
project(Orange)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# 添加子目录
add_subdirectory(Src/OrangeEngine)
add_subdirectory(Src/Game)
add_subdirectory(Src/OrangeEditor)