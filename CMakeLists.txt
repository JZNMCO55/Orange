cmake_minimum_required(VERSION 3.28)

# ---------------------------------------------------------------------------
# OrangeEngine 语义版本号的唯一权威来源。发版时改这里；下面
# `configure_file` 生成的 `OrangeEngineVersion.h` 会把同一组数字带到
# C++ 端（整型 + 字符串宏），后续的 `OrangeEngineConfigVersion.cmake`
# 也通过 CMake 自动派生的 `${PROJECT_VERSION}` 变量回读这里的值。
# ---------------------------------------------------------------------------
project(OrangeEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 统一的输出目录
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# 编译选项
# ---------------------------------------------------------------------------
include(cmake/CompilerOptions.cmake)

# ---------------------------------------------------------------------------
# 依赖
#
# 所有第三方 `find_package` 调用都集中在 `cmake/Dependencies.cmake`；
# 该文件同时记录 PUBLIC / PRIVATE 链接策略，是引擎依赖关系的唯一权
# 威来源。
# ---------------------------------------------------------------------------
include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# OrangeEngine 静态库
# ---------------------------------------------------------------------------
add_library(orange_engine STATIC
    src/EngineStub.cpp

    # Core
    src/core/CoreHeaderCheck.cpp
    src/core/Log.cpp
    src/core/Result.cpp

    # Core：Serialization + Config
    src/core/Config.cpp
    src/core/SchemaVersion.cpp
    src/core/Serialization.cpp

    # Platform：Window（GLFW backend）
    src/platform/PlatformHeaderCheck.cpp
    src/platform/glfw/Window.cpp

    # App：Layer / LayerStack / FrameContext / AppConfig / AppHost
    src/app/AppHeaderCheck.cpp
    src/app/AppHost.cpp
    src/app/Layer.cpp
    src/app/LayerStack.cpp
)

# ---------------------------------------------------------------------------
# 生成 `orange/engine/OrangeEngineVersion.h`。
#
# 输出落在 build tree 的 `${CMAKE_BINARY_DIR}/generated/orange/engine/`，
# 让源码树保持干净（仓库里只保留 .in 模板）。生成目录以 PUBLIC 形式
# 加进 include 路径，使 orange_engine 的下游（samples、tests、安装
# 后通过 find_package 接入的工程）看到的版本头与其他公共头一样位于
# `<orange/engine/...>` 下。`@ONLY` 用来阻止 CMake 把模板里无关的
# `${...}` 当作变量替换。
# ---------------------------------------------------------------------------
set(ORANGE_ENGINE_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/orange/engine/OrangeEngineVersion.h.in"
    "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineVersion.h"
    @ONLY
)

# ---------------------------------------------------------------------------
# `ORANGE_ENGINE_API` 可见性宏
#
# `generate_export_header` 写出的头会把 `ORANGE_ENGINE_API` 定义为：
#   - 当 `ORANGE_ENGINE_STATIC_DEFINE` 被定义时为空（静态库 build）；
#   - 当 CMake 自动注入的 `orange_engine_EXPORTS` 起作用时为
#     `__declspec(dllexport)`（即 SHARED 编译 orange_engine 自身）；
#   - 其它情况下为 `__declspec(dllimport)`（消费 SHARED build 的下游）。
#
# 当前 `BUILD_SHARED_LIBS` 默认 OFF、上面 `add_library` 直接写死
# STATIC，所以宏处处展开为空——对 ABI 没影响、对消费者也无感。这样
# 做的目的是先把宏槽位铺到所有公共类上，将来切到 SHARED build 只需
# 改 CMake 一行，不用再翻所有头文件。
#
# `STATIC_DEFINE` 通过 PUBLIC 与 `INSTALL_INTERFACE` 同时传播：让安
# 装后消费 static SDK 的下游也看到同样为空的宏；否则下游会默认走
# `dllimport` 然后链接出错。
# ---------------------------------------------------------------------------
include(GenerateExportHeader)
generate_export_header(orange_engine
    BASE_NAME            ORANGE_ENGINE
    EXPORT_MACRO_NAME    ORANGE_ENGINE_API
    NO_EXPORT_MACRO_NAME ORANGE_ENGINE_NO_EXPORT
    EXPORT_FILE_NAME     "${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}/orange/engine/OrangeEngineExport.h"
)
target_compile_definitions(orange_engine PUBLIC
    "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:ORANGE_ENGINE_STATIC_DEFINE>"
)

# ---------------------------------------------------------------------------
# Include 路径
#
# `include/` 是公共表面；`src/` 是私有实现。
# `${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}` 以 PUBLIC 暴露，让下游也
# 能在 `<orange/engine/...>` 下看到生成出来的版本头与 export 头。
# ---------------------------------------------------------------------------
target_include_directories(orange_engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${ORANGE_ENGINE_GENERATED_INCLUDE_DIR}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ---------------------------------------------------------------------------
# 链接策略
#
# PUBLIC vs PRIVATE 详细策略写在 cmake/Dependencies.cmake。简述：
#   * OrangeRender / EnTT / glm 出现在公共 API 表面，PUBLIC 链接，
#     消费者继承；
#   * nlohmann_json 仅在 Core::Serialization 内部使用，必须不漏到公
#     共头——PRIVATE 链接；
#   * spdlog（可选）由 Core::Log 包裹；PUBLIC 传播让消费者愿意的话
#     可以复用同一个 logger 实例，与 OrangeRender 的策略对齐；
#   * Box2D / DragonBones / miniaudio / ImGui / stb 一律 PRIVATE——
#     越界禁令见 docs/extension-points.md。
# ---------------------------------------------------------------------------
target_link_libraries(orange_engine
    PUBLIC
        OrangeRender::orange_render
        ${ORANGE_ENGINE_GLM_TARGET}
)

# Soft / 延后依赖——只有 imported target 实际存在时才链。EnTT 在 ECS
# World 落地时会在 Dependencies.cmake 里升 REQUIRED，到时再删除这里
# 的条件分支。
if (TARGET EnTT::EnTT)
    target_link_libraries(orange_engine PUBLIC EnTT::EnTT)
endif ()

# nlohmann_json 在 Dependencies.cmake 里以 REQUIRED 解析，target 必然
# 存在。链接为 PRIVATE，避免类型从公共头泄漏出去。
target_link_libraries(orange_engine PRIVATE nlohmann_json::nlohmann_json)

# glfw3 包裹 Win32 的 window / input 抽取。PRIVATE，让 GLFWwindow*
# 不要逃出 OrangeEngine 的公共头；`Platform::Window` 对外只暴露
# opaque void* native handle。
target_link_libraries(orange_engine PRIVATE glfw)

if (ORANGE_ENGINE_WITH_SPDLOG)
    target_link_libraries(orange_engine PUBLIC spdlog::spdlog)
    target_compile_definitions(orange_engine PUBLIC ORANGE_ENGINE_WITH_SPDLOG=1)
endif ()

if (ORANGE_ENGINE_WITH_TRACY)
    target_link_libraries(orange_engine PRIVATE Tracy::TracyClient)
    target_compile_definitions(orange_engine PRIVATE ORANGE_ENGINE_WITH_TRACY=1)
endif ()

orange_engine_set_compiler_options(orange_engine)

# 命名空间别名，让在同一个 build tree 内的 samples / tests 直接链
# `OrangeEngine::orange_engine`——和安装后通过 find_package(OrangeEngine)
# 看到的目标一致，避免接口偷偷漂移。
add_library(OrangeEngine::orange_engine ALIAS orange_engine)

# ---------------------------------------------------------------------------
# 安装规则
#
# 此处先声明 option；真正的 `install(TARGETS ...)` /
# `OrangeEngineConfig.cmake` 由后续步骤补齐。顶层 build 默认 ON，作为
# `add_subdirectory()` 嵌入时默认 OFF——和 OrangeRender 的策略一致。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_INSTALL "Generate install rules for OrangeEngine" "${PROJECT_IS_TOP_LEVEL}")

# ---------------------------------------------------------------------------
# 测试
#
# 默认 OFF：作为下游 SDK 被消费时，引擎自身的 unit / smoke test 没
# 有理由跟着 build。引擎自己迭代时打开它即可。tests 目录目前只是
# stub，待测试入口落地后再 add_subdirectory。
# ---------------------------------------------------------------------------
option(ORANGE_ENGINE_BUILD_TESTS "Build OrangeEngine unit / smoke tests" OFF)

if (ORANGE_ENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# ---------------------------------------------------------------------------
# Samples
#
# Sample 子工程随各阶段里程碑逐个加入；samples/CMakeLists.txt 内部
# 按顺序 add_subdirectory 每一个 sample。
# ---------------------------------------------------------------------------
add_subdirectory(samples)
