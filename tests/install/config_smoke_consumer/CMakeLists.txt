cmake_minimum_required(VERSION 3.28)

# `find_package(OrangeEngine)` 消费者骨架，由
# tests/install/config_smoke.cmake 驱动。刻意不在主工程的
# `add_subdirectory()` 图里——目标恰恰是证明：一个对源码树零认知的
# 全新 CMake 工程，仅靠 `find_package(OrangeEngine 0.1 CONFIG REQUIRED)`
# 就能拿到完整的 link interface。保持极简——package config 一旦回归，
# 表现是这里的 configure / link 错误，而不是被淹没在外围 build 步骤
# 的噪声里。
project(orange_engine_config_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OrangeEngine 0.1 CONFIG REQUIRED)

if (NOT TARGET OrangeEngine::orange_engine)
    message(FATAL_ERROR
        "config_smoke_consumer: find_package(OrangeEngine CONFIG) 通过了，"
        "但 namespaced target OrangeEngine::orange_engine 不存在。")
endif ()

add_executable(orange_engine_config_smoke main.cpp)
target_link_libraries(orange_engine_config_smoke PRIVATE OrangeEngine::orange_engine)
