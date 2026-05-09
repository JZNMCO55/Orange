# OrangeEditor —— Phase 6 / Task 06-01 起步 target。
#
# **本 CMake 工程刻意是顶级独立工程**（不在 OrangeEngine 主仓 root 的
# add_subdirectory 链上），仅通过 `find_package(OrangeEngine CONFIG)` 消
# 费引擎。这是 Task 06-01 的核心约束 ——
#
#   "编辑器通过 find_package(OrangeEngine) 依赖引擎，不直接吃源；这是
#    引擎 API 自身可消费性的最强验证"
#
# 等同于 game 仓库消费引擎的方式：先 `cmake --install build` 把引擎装
# 到一个 prefix，再用 `-DCMAKE_PREFIX_PATH=<install-prefix>` 配置编辑
# 器。详细 build 流程见同目录 README.md。
#
# CI 验证：tests/install/editor_build_smoke.cmake 自动跑这套两阶段流
# 程，确保 `find_package` 消费链不回归。

cmake_minimum_required(VERSION 3.28)

project(OrangeEditor
    VERSION   0.0.1
    LANGUAGES CXX
    DESCRIPTION "OrangeEngine 关卡 / 粒子 / 材质编辑器（Phase 6 起）")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# `0.1` 系列 ABI 不稳，但 Task 06-01 阶段编辑器只触碰 AppHost 这种最
# 稳定的公共面；将来引擎 API 出现 0.x 内的破坏性变更时，编辑器跟随
# 引擎一同发版本号即可。
find_package(OrangeEngine 0.1 CONFIG REQUIRED)

if (NOT TARGET OrangeEngine::orange_engine)
    message(FATAL_ERROR
        "OrangeEditor: find_package(OrangeEngine CONFIG) 通过了，但 "
        "namespaced target OrangeEngine::orange_engine 不存在 —— "
        "OrangeEngineTargets.cmake 可能损坏。")
endif ()

add_executable(OrangeEditor main.cpp)
target_link_libraries(OrangeEditor PRIVATE OrangeEngine::orange_engine)

# 与 sample / engine 同档的编译器 warning 级别 —— 编辑器作为长期演化
# 的 first-class target，不享受任何"宽松 warning"豁免。
if (MSVC)
    target_compile_options(OrangeEditor PRIVATE /W4 /WX /permissive-)
else ()
    target_compile_options(OrangeEditor PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif ()

# Visual Studio 调试时把 working directory 锚到 .exe 同目录，让将来
# 接入资源加载（Task 06-02 起）时相对路径解析与 Release 行为一致。
set_target_properties(OrangeEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:OrangeEditor>")
