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
# ImGui —— 复用引擎统一 vendor 的那一份（OrangeEngine::imgui）
#
# 历史上编辑器自己 FetchContent + 编 orange_editor_imgui。自
# GAP-2026-05-27-consumer-imgui-tuning-hook 起，引擎把 ImGui 上提为唯一
# owner（PUBLIC 暴露给消费者，供游戏侧 Layer::OnImGui 用），编辑器随之改为
# **复用** OrangeEngine::imgui——否则两份静态 imgui 在本 exe 链接期撞重复符号
# （ODR）。imgui 头（imgui.h / imgui_impl_glfw.h / imgui_impl_vulkan.h）经
# OrangeEngine::imgui 的 PUBLIC/INTERFACE include 传递性可见，main.cpp /
# EditorRenderLayer.cpp 的 ImGui 调用代码无需改动。
#
# in-tree（开发期主路径）：OrangeEngine::imgui 由父 CMake 创建（ALIAS）；
# standalone consumer（editor_build_smoke）：由 find_package(OrangeEngine) 的
# OrangeEngineTargets.cmake 引入（imgui 在 export set 内、头随 SDK 安装）。
#
# Vulkan 头 / glfw：编辑器自身 TU（VulkanLoaderShim.cpp 等）直接 #include
# <vulkan/vulkan.h> + 与 AppHost 共享 GLFWwindow*，仍需各自 find。
# ---------------------------------------------------------------------------
find_package(glfw3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# OrangeEditor exe
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# orange_editor STATIC lib（M3 lib 化）—— 承载全部编辑器子系统 + 应用装配
# （EditorApp.cpp = Orange::Editor::RunEditorApp）。瘦 OrangeEditor.exe 与
# per-game editor（M4 起 SlimeEditor.exe）都只链本 lib + 各自 3 行瘦 main，
# 避免复制 ~1100 行 main() 装配逻辑。
#
# MikkTSpace（vendor/mikktspace/mikktspace.c）以 C 编译——C 语言在顶层
# project(... LANGUAGES C CXX) 启用（见根 CMakeLists 注释）。纯 C 编译让
# genTangSpaceDefault 天然 C linkage，与 MeshTangentGen.cpp 经 mikktspace.h
# `extern "C"` 的引用精确匹配。其 warning 见下方 set_source_files_properties /W0。
# ---------------------------------------------------------------------------
add_library(orange_editor STATIC
    EditorApp.cpp
    BuiltinAssets.cpp
    DemoWorld.cpp
    ColliderDebugDraw.cpp
    ColliderVertexEdit.cpp
    CoplanarDetector.cpp
    EditorAssetReferences.cpp
    EditorCameraControl.cpp
    EditorHierarchy.cpp
    EditorGizmoMath.cpp
    EditorPicking.cpp
    EditorPrefabActions.cpp
    PrefabOverrideUI.cpp
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
    panels/AnimationTimelinePanel.cpp
    panels/EntityTreePanel.cpp
    panels/InspectorPanel.cpp
    panels/LayersPanel.cpp
    panels/RenderSettingsPanel.cpp
    panels/ScenePanel.cpp
    panels/ToolbarPanel.cpp
    command/AnimFsmCommands.cpp
    command/CommandStack.cpp
    command/EntityCommands.cpp
    command/PrefabCommands.cpp
    command/SetAnimationClipCommand.cpp
    # MCP 实时协同桥（ADR-020）—— 后台 socket 命令端（winsock 集中在 McpServer.cpp）
    # + 主线程帧末命令执行器。Python MCP server 在 tools/orange-mcp/（不进本 build）。
    mcp/McpServer.cpp
    mcp/McpCommandHandler.cpp
    import/FbxAxisConverter.cpp
    import/FbxImporter.cpp
    import/FbxMaterialParse.cpp
    import/FbxSceneImporter.cpp
    import/GltfImporter.cpp
    import/GltfMaterialParse.cpp
    import/GltfSceneImporter.cpp
    import/ImportDispatcher.cpp
    import/ImportHostBridge.cpp
    import/MeshTangentGen.cpp
    import/MetaSidecar.cpp
    import/ObjImporter.cpp
    # MikkTSpace 标准切线算法（vendor/mikktspace，zlib 许可，C 源）。与
    # cgltf / tinyobjloader 同款 in-tree vendor。仅 OrangeEditor 消费——引擎
    # runtime 不接（ADR-008 importer-only invariant）。warning 见下方 /W0。
    ../../vendor/mikktspace/mikktspace.c
    # OpenFBX（vendor/OpenFBX，MIT）—— FBX 解析。不像 cgltf/tinyobjloader 是纯
    # header：ofbx.cpp（C++）+ libdeflate.c（C，ofbx.cpp 本地 include）需编译。
    # 仅 OrangeEditor 消费（ADR-008 importer-only invariant）。vendored 源**不改**，
    # warning 用下方 per-TU /W0 压（C/C++ 混编：libdeflate.c 走 C 编译器）。
    ../../vendor/OpenFBX/ofbx.cpp
    ../../vendor/OpenFBX/libdeflate.c
    render/EditorGridAuxPassProvider.cpp
    render/ThumbnailService.cpp
    schema/AssetRefSideEffects.cpp
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
    plugin/ScriptFieldOverridesInspectorPlugin.cpp
    plugin/DirectionalLightGizmoPlugin.cpp
    plugin/ImportMetaAssetInspectorPlugin.cpp
    plugin/MaterialAssetInspectorPlugin.cpp
    plugin/ParticleEmitterGizmoPlugin.cpp
    plugin/PointLightGizmoPlugin.cpp
    plugin/PostProcessVolumeGizmoPlugin.cpp
    plugin/SpotLightGizmoPlugin.cpp
    demo_game/HealthComponent.cpp
    branding/EditorWindowIcon.cpp
)
# OrangeRender::orange_render 由 OrangeEngine::orange_engine 的公共 link 自动
# 传递；不重复 explicit link，避免 static initializer 顺序冲突。**PUBLIC** 让
# exe 与 per-game editor 经链 orange_editor 传递性拿到 引擎 / imgui / vulkan /
# glfw —— STATIC lib 不嵌依赖对象码，最终 exe link 仍需自行解析全部符号。
target_link_libraries(orange_editor PUBLIC
    OrangeEngine::orange_engine
    OrangeEngine::imgui
    Vulkan::Vulkan
    glfw)

# Build-tree 命名空间别名 —— 让同一 build tree 内的 in-tree 消费者（本仓
# OrangeEditor.exe，将来 per-game editor 若走 add_subdirectory）与安装后经
# find_package(OrangeEditor) 看到的 OrangeEditor::orange_editor 同名，避免接口
# 偷偷漂移。无条件创建（与引擎 OrangeEngine::orange_engine ALIAS 同款）。
add_library(OrangeEditor::orange_editor ALIAS orange_editor)

# orange_editor 的公共 API 面极小 —— per-game editor 瘦 main 只 #include
# EditorApp.h + EditorAppConfig.h（两者直接在 tools/OrangeEditor/ 根）。给 lib
# 加 PUBLIC include：in-tree 走 BUILD_INTERFACE（源树目录，本 exe 早已隐式可
# 见），安装后走 INSTALL_INTERFACE（include/orange_editor，仅装那两个头，见
# 文件末 install 块）。上方 vendor include 仍是 PRIVATE，不外泄给消费者。
target_include_directories(orange_editor PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:include/orange_editor>")

# ---------------------------------------------------------------------------
# OrangeEditor.exe —— 瘦 main（仅转发 argc/argv 进 orange_editor::RunEditorApp）。
# Windows resource：把 orange-editor-logo.ico 嵌进 exe。MSVC / CMake 自动识别
# .rc 后缀、调 rc.exe 编出 .res、link 到 exe；非 MSVC（Linux / Clang on Linux）
# 下自动忽略——本仓当前 Windows-only 不影响。资源留 **exe**（不进 STATIC lib）。
# 重建 ICO：跑 `python tools/OrangeEditor/branding/build_icon.py`。
# ---------------------------------------------------------------------------
add_executable(OrangeEditor
    main.cpp
    branding/OrangeEditor.rc)
target_link_libraries(OrangeEditor PRIVATE orange_editor)

# v1.1 T3：tinyobjloader 单 header（MIT）vendored 到 `vendor/tinyobjloader/`，
# 与 vendor/stb 同款 in-tree single-header 模式（不走 submodule，体积可控
# ~467KB 单文件）。include path 只暴露给 orange_editor（PRIVATE，importer TU
# 在 lib 内）—— 引擎 runtime 不消费 tinyobjloader（ADR-008 invariant "importer
# 全部在 tools/OrangeEditor/import/，不污染 src/asset/"）。
# CMAKE_CURRENT_LIST_DIR 是 tools/OrangeEditor/，向上两级就是 engine repo
# 根，无论 in-tree 还是 standalone consumer build 模式都稳定指对。
# v1.1 T4：cgltf 单 header（MIT）同样 in-tree 模式接入。
target_include_directories(orange_editor PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/tinyobjloader"
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/cgltf"
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/mikktspace"
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/OpenFBX")

# MikkTSpace 是第三方 C 源——不享受编辑器的 /W4 /WX。单独把它的 warning
# 关掉（与 orange_editor_imgui 的 /W0 同款豁免），避免 /WX 把 vendor 代码
# 的 C4xxx 升级成 error。只作用于这一个源文件，编辑器自身代码仍 /W4 /WX。
if (MSVC)
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/mikktspace/mikktspace.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/mikktspace/mikktspace.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/../../vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()

# 与 sample / engine 同档的编译器 warning 级别 —— 编辑器作为长期演化
# 的 first-class target，不享受任何"宽松 warning"豁免。lib（子系统全量）+
# exe（瘦 main）同档。
if (MSVC)
    target_compile_options(orange_editor PRIVATE /W4 /WX /permissive-)
    target_compile_options(OrangeEditor PRIVATE /W4 /WX /permissive-)
else ()
    target_compile_options(orange_editor PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
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
# lib 也依赖 shader 编译目标 —— orange_editor_shaders 本已是 ALL（全量 build
# 必编），此依赖是双保险：即便只 build lib（`--target orange_editor`）也触发
# spv 编出，保证下方 install(FILES ${...SPV}) 的输入在 `cmake --install` 时已存在。
# per-game editor 消费者不编 shader，靠 install + Config copy-helper 拿 spv。
add_dependencies(orange_editor orange_editor_shaders)

# ---------------------------------------------------------------------------
# 安装 / 导出规则 —— 让 per-game editor（OG SlimeEditor.exe，M4 起）能
# `find_package(OrangeEditor CONFIG)` + 链 OrangeEditor::orange_editor 建自己的
# 编辑器 exe，复用 orange_editor lib 承载的全部 ~1100 行 main() 装配逻辑，只写
# 各自 3 行瘦 main。整块纯附加，不影响现有 in-tree build。
#
# 默认跟随引擎的 ORANGE_ENGINE_INSTALL：in-tree 时编辑器由 root
# add_subdirectory 拉入、其 project(OrangeEditor) 令本目录 PROJECT_IS_TOP_LEVEL
# 为假，故不能拿它当默认——改跟随引擎 install 开关，保证 `cmake --install` 引擎
# 时编辑器 lib + Config 一同装出。standalone consumer（editor_build_smoke，编辑器
# 为 top-level）下 ORANGE_ENGINE_INSTALL 未定义，退回 PROJECT_IS_TOP_LEVEL。
#
# 安装版图（GNUInstallDirs，与引擎同 prefix）：
#   <prefix>/lib/                                       —— orange_editor 静态归档
#   <prefix>/include/orange_editor/                     —— EditorApp.h + EditorAppConfig.h
#   <prefix>/lib/cmake/OrangeEditor/                    —— Targets / Config / 版本文件
#   <prefix>/share/OrangeEditor/shaders/orange_editor/  —— grid/fullscreen SPIR-V
#   <prefix>/share/OrangeEditor/theme/codicons/         —— codicon 字体 + LICENSE
# ---------------------------------------------------------------------------
if (DEFINED ORANGE_ENGINE_INSTALL)
    set(_orange_editor_install_default "${ORANGE_ENGINE_INSTALL}")
else ()
    set(_orange_editor_install_default "${PROJECT_IS_TOP_LEVEL}")
endif ()
option(ORANGE_EDITOR_INSTALL
    "Generate install / export rules for OrangeEditor (lib + Config for per-game editors)"
    "${_orange_editor_install_default}")

if (ORANGE_EDITOR_INSTALL)
    include(GNUInstallDirs)
    set(ORANGE_EDITOR_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/OrangeEditor")

    # lib + export set。orange_editor PUBLIC 链的 OrangeEngine:: / Vulkan:: /
    # glfw 全是 imported / alias target（非本工程本地 target），无需进本 export
    # set；消费者侧由 OrangeEditorConfig 的 find_dependency 链重新解析。
    install(TARGETS orange_editor
        EXPORT   OrangeEditorTargets
        ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )

    # 公共 API 头 —— 装**整棵**编辑器头树（保留子目录结构）到 include/orange_editor/
    # （与 lib 的 INSTALL_INTERFACE include 对齐）。
    #
    # 为什么整树而非仅 EditorApp.h/EditorAppConfig.h：瘦 main 确实只需那两个，但
    # M4 手感组件化让 per-game editor（OG）要用 `schema/ComponentSchemaRegistry.h`
    # 的 ComponentSchemaBuilder 注册自家游戏组件 Inspector schema——该头 include
    # `../EditorHost.h`，后者又拖 context/ + command/ + plugin/ 一串编辑器内部头。
    # 故 schema 扩展点的公共面 = 编辑器头树整体。相对 include（"../EditorHost.h"）
    # 靠保留子目录结构在 include/orange_editor/ 内自洽解析。这是 roadmap 开放
    # 问题#4"编辑器头公共面粒度"的取舍：全量导出起步，后续可挑子集收敛。
    # 只装 *.h（.rc / .cpp / vendor 源不装）。
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/orange_editor"
        FILES_MATCHING PATTERN "*.h"
    )

    # 编辑器内置 SPIR-V（grid + fullscreen）—— 用 FILES 因变量含 $<CONFIG>
    # genex（install(DIRECTORY) 不吃 genex）。消费者靠 Config 的
    # orange_editor_copy_editor_shaders() 拷到自己 .exe 旁 shaders/orange_editor/。
    install(FILES
            "${ORANGE_EDITOR_FULLSCREEN_VS_SPV}"
            "${ORANGE_EDITOR_GRID_FS_SPV}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/OrangeEditor/shaders/orange_editor"
    )

    # codicon 图标字体（+ 许可）—— 编辑器 runtime 按 ".exe 相对
    # theme/codicons/codicon.ttf" 加载（EditorApp.cpp exe-相对优先）。消费者靠
    # Config 的 orange_editor_copy_theme() 拷到自己 .exe 旁 theme/。
    install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/theme/codicons/codicon.ttf"
            "${CMAKE_CURRENT_SOURCE_DIR}/theme/codicons/LICENSE-codicons.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/OrangeEditor/theme/codicons"
    )

    install(EXPORT OrangeEditorTargets
        FILE        OrangeEditorTargets.cmake
        NAMESPACE   OrangeEditor::
        DESTINATION "${ORANGE_EDITOR_INSTALL_CMAKE_DIR}"
    )

    # -----------------------------------------------------------------------
    # OrangeEditorConfig.cmake + 版本文件
    #
    # 版本用 OrangeEditor 自己 project() 声明的版本（OrangeEditor_VERSION =
    # 1.3.0），**不是**引擎的 PROJECT_VERSION —— in-tree add_subdirectory 时
    # 顶层 CMAKE_PROJECT_VERSION 是引擎的 0.1.0，而 <PROJECT-NAME>_VERSION 恒为
    # 本 project(OrangeEditor VERSION ...) 声明值，故显式用后者避歧义。
    #
    # SameMinorVersion：消费者 find_package(OrangeEditor 1.3 ...) 接受 1.3.x、
    # 拒绝 1.4.x，与引擎 Config 同档兼容策略。
    #
    # 模板在引擎仓根 cmake/OrangeEditorConfig.cmake.in（与 OrangeEngineConfig
    # 同目录）；从 tools/OrangeEditor/ 向上两级到达，in-tree / standalone 两种
    # configure 语境下 CMAKE_CURRENT_SOURCE_DIR 都稳定指对。
    # -----------------------------------------------------------------------
    include(CMakePackageConfigHelpers)

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OrangeEditorConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEditorConfig.cmake"
        INSTALL_DESTINATION "${ORANGE_EDITOR_INSTALL_CMAKE_DIR}"
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEditorConfigVersion.cmake"
        VERSION       ${OrangeEditor_VERSION}
        COMPATIBILITY SameMinorVersion
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEditorConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/OrangeEditorConfigVersion.cmake"
        DESTINATION "${ORANGE_EDITOR_INSTALL_CMAKE_DIR}"
    )
endif ()
