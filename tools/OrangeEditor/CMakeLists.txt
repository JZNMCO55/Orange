# OrangeEditor —— OrangeEngine 关卡 / 粒子 / 材质编辑器。
#
# 双模 CMake 工程，两条路径都受支持，二者不互斥：
#
#   * **In-tree（开发期主路径）**：被引擎仓 root 的 `add_subdirectory(tools/OrangeEditor)`
#     拉进同一 sln，跟引擎共享 build/configure 周期。改一行引擎源码 →
#     直接 F5 编辑器，免去两阶段 install。由根 CMake 的
#     `ORANGE_ENGINE_BUILD_EDITOR` option 控制（顶层 build 时默认 ON）。
#
#   * **Standalone consumer（CI 验证路径）**：本目录作为顶级 CMake 工程
#     独立 configure，通过 `find_package(OrangeEngine CONFIG)` 消费已
#     install 的引擎。这是引擎公共 API 可用性的最强验证——
#
#       "编辑器通过 find_package(OrangeEngine) 依赖引擎，不直接吃源；这是
#        引擎 API 自身可消费性的最强验证"
#
#     用法：先 `cmake --install build --prefix build/install_tree` 装
#     引擎，再用 `-DCMAKE_PREFIX_PATH=<install-prefix>` 配置本目录。
#     详细 build 流程见同目录 README.md。
#
# CI 验证：tests/install/editor_build_smoke.cmake 自动跑 standalone
# 路径，确保 `find_package` 消费链不回归。
#
# 不论哪条路径，编辑器仅链 `OrangeEngine::orange_engine` 公共 namespaced
# target——CMake 的 target_include_directories PRIVATE 把 `src/` 关在外
# 面，编辑器看不到引擎 internal 头，API 纪律靠 target visibility 守。

cmake_minimum_required(VERSION 3.28)

project(OrangeEditor
    VERSION   1.3.0
    # C 语言必须在本 project 显式启用 —— vendor/mikktspace/mikktspace.c 是
    # C 源，需 C linkage 才能让 MeshTangentGen.cpp 经 mikktspace.h 的
    # extern "C" 正确解析 genTangSpaceDefault。in-tree 构建靠根 project
    # 的 LANGUAGES C CXX 兜住，但 OrangeEditor 作为独立 consumer 工程
    # （editor_build_smoke / 第三方 find_package 消费）单独 configure 时
    # 拿不到根的 C 启用，会漏编 mikktspace.c → 链接 unresolved external。
    LANGUAGES C CXX
    DESCRIPTION "OrangeEngine 关卡 / 粒子 / 材质编辑器")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 双模守卫：in-tree 路径下 OrangeEngine::orange_engine 已由父 CMake 创
# 建（根 CMakeLists 的 ALIAS），跳过 find_package；standalone 路径下走
# `find_package(OrangeEngine 0.1 CONFIG REQUIRED)` 从已 install 的 SDK
# 拿同名 IMPORTED target。`0.1` 系列 ABI 不稳，将来 0.x 内出现破坏性
# 变更时编辑器跟随引擎一同发版本号即可。
if (NOT TARGET OrangeEngine::orange_engine)
    find_package(OrangeEngine 0.1 CONFIG REQUIRED)
    if (NOT TARGET OrangeEngine::orange_engine)
        message(FATAL_ERROR
            "OrangeEditor: find_package(OrangeEngine CONFIG) 通过了，但 "
            "namespaced target OrangeEngine::orange_engine 不存在 —— "
            "OrangeEngineTargets.cmake 可能损坏。")
    endif ()
endif ()

# OrangeRender 由 OrangeEngine 的 find_dependency 拉进来；编辑器 ImGui
# 接入需要走 Orange::Renderer::Interop 公共面拿 VkInstance / VkDevice
# 等 raw Vulkan handle，因此显式确认 Renderer 公共头可见 + 绑定 namespaced
# target 到 link line（in-tree 时父 CMake 已经 find_package 过，重复
# find_package 是无害的 cache hit；同样守一道 guard 让意图清晰）。
if (NOT TARGET OrangeRender::orange_render)
    find_package(OrangeRender 0.1 CONFIG REQUIRED)
endif ()

# Vulkan SDK headers —— ImGui Vulkan backend 直接 #include <vulkan/vulkan.h>，
# 编辑器自己也需要 cast Interop::* void* 回 VkInstance 等。OrangeRender
# 的 transitive link 会带 vulkan_loader，但 include path 需要单独 find。
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------------------
# ImGui (docking branch) —— Phase 6 / Task 06-02：编辑器整套 UI 走 ImGui。
# 用 FetchContent 拉取，pin 一个稳定的 docking-branch commit，避免编辑器
# 集成跟随上游 mainline 漂移。
# ---------------------------------------------------------------------------

include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    # docking 分支的稳定 tag —— v1.91.5-docking。pin 具体 sha 让 build 可重复
    GIT_TAG        v1.91.5-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ImGui 仓没自带 CMakeLists，手工把 core + impl_glfw + impl_vulkan 编成
# 一个静态 lib。
#
# 关键编译宏 IMGUI_IMPL_VULKAN_NO_PROTOTYPES：让 impl_vulkan 不再 extern
# 引用 vulkan-1.lib 的静态 vkXxx 符号，转为模块内函数指针变量；启动期
# `ImGui_ImplVulkan_LoadFunctions` 通过一个 loader callback 把所有 vkXxx
# 解析到运行时函数指针。这样 ImGui 跟 OrangeRender 共用同一个 volk loader
# 解析路径，避免"消费者侧静态 vulkan-1.lib stub 与 OrangeRender 内部 volk
# 已加载的 loader entry 是两条独立解析路径"的 dispatch 状态错位。
# 配套的 loader callback + Interop::GetVulkanGetInstanceProcAddr() 在
# main.cpp 内串起来。
add_library(orange_editor_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(orange_editor_imgui PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
# PRIVATE 即可——头文件 imgui_impl_vulkan.h 不会因为消费者侧没定义
# NO_PROTOTYPES 而 ABI 漂移（仅 struct 定义 + 函数声明，不含 inline 调
# 用 vkXxx 的代码）。把宏放 PRIVATE 让 main.cpp 仍能 include vulkan.h
# 拿到 PFN_* / Vk* 类型，但不会再链 vulkan-1.lib 的 vkXxx 静态 stub。
target_compile_definitions(orange_editor_imgui PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
# Vulkan::Vulkan 提供 vulkan.h 的 include path；vulkan-1.lib 的链接在
# NO_PROTOTYPES 下不再被 ImGui TU 引用（主 exe 也不直接调 vk*，全部走
# Interop::GetVulkanGetInstanceProcAddr() 解析出的运行时指针），剩下的
# 静态链是无害死重，留着以免破坏 include path 的传递。
target_link_libraries(orange_editor_imgui PUBLIC Vulkan::Vulkan)
# ImGui core 本身不需要 OrangeEngine —— 只 impl_glfw 需要 GLFW headers。
# GLFW 是 OrangeRender 的 PRIVATE 依赖；编辑器自己也开窗（AppHost 内
# 部用 GLFW，编辑器跟它共享同一 GLFWwindow*）所以需要直接 find。
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(orange_editor_imgui PUBLIC glfw)

# 关掉 ImGui 自身的 warning（避免编辑器的 /WX /Werror 把 ImGui 内部
# 警告升级为 error）。
if (MSVC)
    target_compile_options(orange_editor_imgui PRIVATE /W0)
else ()
    target_compile_options(orange_editor_imgui PRIVATE -w)
endif ()

# ---------------------------------------------------------------------------
# OrangeEditor exe
# ---------------------------------------------------------------------------

# MikkTSpace（vendor/mikktspace/mikktspace.c）以 C 编译——C 语言在顶层
# project(... LANGUAGES C CXX) 启用（见根 CMakeLists 注释）。纯 C 编译让
# genTangSpaceDefault 天然 C linkage，与 MeshTangentGen.cpp 经 mikktspace.h
# `extern "C"` 的引用精确匹配。其 warning 见下方 set_source_files_properties /W0。
add_executable(OrangeEditor
    main.cpp
    BuiltinAssets.cpp
    DemoWorld.cpp
    ColliderDebugDraw.cpp
    ColliderVertexEdit.cpp
    CoplanarDetector.cpp
    EditorCameraControl.cpp
    EditorHierarchy.cpp
    EditorGizmoMath.cpp
    EditorPicking.cpp
    EditorRenderLayer.cpp
    EditorRotateGizmo.cpp
    EditorScaleGizmo.cpp
    EditorTranslateGizmo.cpp
    EditorWidgets.cpp
    AnimFsmFileIO.cpp
    EditorAssetDropHandler.cpp
    MaterialFileIO.cpp
    ShaderTemplateMetaIO.cpp
    VulkanLoaderShim.cpp
    context/EditorKeybindings.cpp
    context/EditorSettings.cpp
    panels/EntityTreePanel.cpp
    panels/InspectorPanel.cpp
    panels/LayersPanel.cpp
    panels/ScenePanel.cpp
    panels/ToolbarPanel.cpp
    command/AnimFsmCommands.cpp
    command/CommandStack.cpp
    command/EntityCommands.cpp
    import/GltfImporter.cpp
    import/ImportDispatcher.cpp
    import/MeshTangentGen.cpp
    import/MetaSidecar.cpp
    import/ObjImporter.cpp
    # MikkTSpace 标准切线算法（vendor/mikktspace，zlib 许可，C 源）。与
    # cgltf / tinyobjloader 同款 in-tree vendor。仅 OrangeEditor 消费——引擎
    # runtime 不接（ADR-008 importer-only invariant）。warning 见下方 /W0。
    ../../vendor/mikktspace/mikktspace.c
    render/EditorGridAuxPassProvider.cpp
    schema/ComponentSchemaRegistry.cpp
    schema/RegisterBuiltinSchemas.cpp
    schema/SchemaInspector.cpp
    theme/EditorTheme.cpp
    plugin/AnimFsmAssetInspectorPlugin.cpp
    plugin/AnimatorMiniPreviewPlugin.cpp
    plugin/AudioAssetInspectorPlugin.cpp
    plugin/AudioSourceInspectorPlugin.cpp
    plugin/DragonBonesAssetInspectorPlugin.cpp
    plugin/CameraFrustumGizmoPlugin.cpp
    plugin/ColliderEditInspectorPlugin.cpp
    plugin/DirectionalLightGizmoPlugin.cpp
    plugin/ImportMetaAssetInspectorPlugin.cpp
    plugin/MaterialAssetInspectorPlugin.cpp
    plugin/ParticleEmitterGizmoPlugin.cpp
    plugin/PointLightGizmoPlugin.cpp
    plugin/SpotLightGizmoPlugin.cpp
    demo_game/HealthComponent.cpp
    branding/EditorWindowIcon.cpp
    # Windows resource：把 orange-editor-logo.ico 嵌进 exe。MSVC / CMake 自
    # 动识别 .rc 后缀、调 rc.exe 编出 .res、link 到 exe；非 MSVC（Linux /
    # Clang on Linux）下会被自动忽略——本仓库当前 Windows-only 不影响。
    # 重建 ICO：跑 `python tools/OrangeEditor/branding/build_icon.py`。
    branding/OrangeEditor.rc
)
# OrangeRender::orange_render 由 OrangeEngine::orange_engine 的公共
# link 自动传递；不重复 explicit link，避免 static initializer 顺序冲突。
target_link_libraries(OrangeEditor PRIVATE
    OrangeEngine::orange_engine
    orange_editor_imgui
    glfw)

# v1.1 T3：tinyobjloader 单 header（MIT）vendored 到 `vendor/tinyobjloader/`，
# 与 vendor/stb 同款 in-tree single-header 模式（不走 submodule，体积可控
# ~467KB 单文件）。include path 只暴露给 OrangeEditor target —— 引擎 runtime
# 不消费 tinyobjloader（ADR-008 invariant "importer 全部在 tools/OrangeEditor/
# import/，不污染 src/asset/"）。
# CMAKE_CURRENT_LIST_DIR 是 tools/OrangeEditor/，向上两级就是 engine repo
# 根，无论 in-tree 还是 standalone consumer build 模式都稳定指对。
# v1.1 T4：cgltf 单 header（MIT）同样 in-tree 模式接入。
target_include_directories(OrangeEditor PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/tinyobjloader"
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/cgltf"
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/mikktspace")

# MikkTSpace 是第三方 C 源——不享受编辑器的 /W4 /WX。单独把它的 warning
# 关掉（与 orange_editor_imgui 的 /W0 同款豁免），避免 /WX 把 vendor 代码
# 的 C4xxx 升级成 error。只作用于这一个源文件，编辑器自身代码仍 /W4 /WX。
if (MSVC)
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()

# 与 sample / engine 同档的编译器 warning 级别 —— 编辑器作为长期演化
# 的 first-class target，不享受任何"宽松 warning"豁免。
if (MSVC)
    target_compile_options(OrangeEditor PRIVATE /W4 /WX /permissive-)
else ()
    target_compile_options(OrangeEditor PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif ()

# Visual Studio 调试时把 working directory 锚到 .exe 同目录，让将来
# 接入资源加载时相对路径解析与 Release 行为一致。
set_target_properties(OrangeEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:OrangeEditor>")

# ---------------------------------------------------------------------------
# OrangeEditor 内置 shader 编译（v1.3.0 引入）—— grid pass + fullscreen
# vertex 从 OrangeEngine `src/render/builtin_shaders/` 迁出，编辑器自家
# 路径独立编译落到 `shaders/orange_editor/`，与 engine 端 `shaders/orange_engine/`
# 平行不冲突。EditorGridAuxPassProvider 在 Initialize 时按
# "shaders/orange_editor/<name>.spv" 相对 .exe 目录解析。
#
# glslangValidator 由 LunarG Vulkan SDK 提供；与 engine 端同款 find_program
# + add_custom_command 模板，仅 OUTPUT_DIR 改名隔离。
# ---------------------------------------------------------------------------
find_program(ORANGE_EDITOR_GLSLANG_VALIDATOR
    NAMES glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)
if (NOT ORANGE_EDITOR_GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "OrangeEditor: 没在 VULKAN_SDK 下找到 glslangValidator。"
        "Editor 渲染 aux pass shader 需要它编 SPIR-V。请安装 LunarG "
        "Vulkan SDK 并设置环境变量 VULKAN_SDK。")
endif ()

set(ORANGE_EDITOR_SHADER_OUTPUT_DIR
    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/shaders/orange_editor")

function(orange_editor_compile_shader src_relative out_var)
    set(_src "${CMAKE_CURRENT_LIST_DIR}/${src_relative}")
    get_filename_component(_full_name "${src_relative}" NAME)
    string(REGEX REPLACE "\\.glsl$" "" _stem "${_full_name}")
    set(_out "${ORANGE_EDITOR_SHADER_OUTPUT_DIR}/${_stem}.spv")
    add_custom_command(
        OUTPUT  "${_out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${ORANGE_EDITOR_SHADER_OUTPUT_DIR}"
        COMMAND "${ORANGE_EDITOR_GLSLANG_VALIDATOR}" -V
                "${_src}" -o "${_out}"
        DEPENDS "${_src}"
        COMMENT "OrangeEditor: 编译内置 SPIR-V → ${_stem}.spv"
        VERBATIM
    )
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

orange_editor_compile_shader(
    "shaders/fullscreen.vert.glsl"
    ORANGE_EDITOR_FULLSCREEN_VS_SPV)
orange_editor_compile_shader(
    "shaders/grid.frag.glsl"
    ORANGE_EDITOR_GRID_FS_SPV)

add_custom_target(orange_editor_shaders ALL
    DEPENDS
        "${ORANGE_EDITOR_FULLSCREEN_VS_SPV}"
        "${ORANGE_EDITOR_GRID_FS_SPV}")
add_dependencies(OrangeEditor orange_editor_shaders)
