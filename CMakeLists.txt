cmake_minimum_required(VERSION 3.15)
project(MyProject)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加子目录
add_subdirectory(Src/OrangeEngine)
add_subdirectory(Src/Game)