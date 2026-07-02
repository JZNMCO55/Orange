# OrangeEngine 测试入口。仅在顶层 ORANGE_ENGINE_BUILD_TESTS=ON 时被
# add_subdirectory 进来；调用方已经 enable_testing() 了。
#
# 为了避免引入完整的测试框架（Catch2 / GoogleTest 等），用最小可用
# 的 `<cassert>` + 独立 main() 模式：每个 test 自己是一个可执行
# target，进程 exit code 0 即通过。这与 OrangeRender 的测试约定对齐。

include_guard(DIRECTORY)

# orange_engine_add_test(<target_name> <source.cpp>)
#
# 注册一个测试可执行体并通过 add_test 暴露给 ctest。target name 同
# 时是 ctest 的 case 名，方便 `ctest -R` 过滤。
function(orange_engine_add_test target_name source)
    add_executable(${target_name} ${source})
    target_link_libraries(${target_name} PRIVATE OrangeEngine::orange_engine)
    # tests 根作为 include 根：让测试可写 #include "support/EngineTestAssert.h"
    # / #include "golden/GoldenTestHarness.h"（手写 add_executable 的测试各自补
    # 同款一行）。
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    orange_engine_set_compiler_options(${target_name})
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

orange_engine_add_test(core_result_test         core/ResultTest.cpp)
orange_engine_add_test(core_handle_test         core/HandleTest.cpp)
orange_engine_add_test(core_serialization_test  core/SerializationTest.cpp)
orange_engine_add_test(asset_registry_test      asset/AssetRegistryTest.cpp)
orange_engine_add_test(async_asset_test         asset/AsyncAssetTest.cpp)
orange_engine_add_test(mesh_tangent_loader_test asset/MeshTangentLoaderTest.cpp)
# Animation：AnimationSystem / TickAnimators 的 ECS 集成（World 上所有
# AnimatorComponent 每帧 tick）。纯 orange_engine（World + AnimatorComponent +
# ProceduralAnimator + MaterialInstance），无 Vulkan/GUI/dragonbones。
orange_engine_add_test(animation_system_test    animation/AnimationSystemTest.cpp)
orange_engine_add_test(spirv_disk_loader_test  asset/SpirvDiskLoaderTest.cpp)

# MikkTSpace 切线生成（A2 Inc2）—— 测的是 OrangeEditor import helper
# （MeshTangentGen）的回调接线 + re-weld，故直接编译该 helper 源 + vendor
# 的 mikktspace.c（与下方 material_file_io_test 编 editor helper 同款模式）。
# mikktspace.c 是第三方 C 源（C 语言在顶层 project 启用），单独 /W0 豁免
# /WX。helper 自身 + 测试源仍走标准 /W4 /WX。
add_executable(mikktspace_tangent_test
    asset/MikkTSpaceTangentTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c")
target_link_libraries(mikktspace_tangent_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(mikktspace_tangent_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace")
orange_engine_set_compiler_options(mikktspace_tangent_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME mikktspace_tangent_test COMMAND mikktspace_tangent_test)

# MikkTSpace 真实复杂模型验证 —— 在有机曲面上跑 cgltf 解析 + 切线生成，断
# 言全覆盖 + 单位长 + 切线⊥法线。fixture（gltf_test/Avocado）是 session 内
# 下载的未跟踪资产，故 `if(EXISTS)` 门控：本机有就编 + 跑，干净 checkout 跳
# 过（不阻塞 CI）。cgltf 同款单 header（CGLTF_IMPLEMENTATION 在测试 TU expand）。
set(_orange_mikkt_fixture "${CMAKE_SOURCE_DIR}/gltf_test/Avocado/Avocado.gltf")
if (EXISTS "${_orange_mikkt_fixture}")
    add_executable(mikktspace_real_model_test
        asset/MikkTSpaceRealModelTest.cpp
        "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c")
    target_link_libraries(mikktspace_real_model_test PRIVATE OrangeEngine::orange_engine)
    target_include_directories(mikktspace_real_model_test PRIVATE
        "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
        "${CMAKE_SOURCE_DIR}/vendor/cgltf")
    target_compile_definitions(mikktspace_real_model_test PRIVATE
        "ORANGE_ENGINE_GLTF_FIXTURE=\"${_orange_mikkt_fixture}\"")
    orange_engine_set_compiler_options(mikktspace_real_model_test)
    if (MSVC)
        set_source_files_properties(
            "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
            PROPERTIES COMPILE_FLAGS "/W0")
    else ()
        set_source_files_properties(
            "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
            PROPERTIES COMPILE_FLAGS "-w")
    endif ()
    add_test(NAME mikktspace_real_model_test COMMAND mikktspace_real_model_test)
else ()
    message(STATUS "mikktspace_real_model_test 跳过：未找到 fixture ${_orange_mikkt_fixture}")
endif ()

# glTF material 导入端到端 —— 锁 "glTF material factor / 贴图 → .material 字段"
# 链（GAP-2026-05-25 G3 验证 + AO factor 修复回归守卫）。走 GltfMaterialParse
# seam（ExtractGltfMaterial / BuildMaterialFileData）+ MaterialFileIO round-trip，
# 不链接整套 EditorHost（AudioEngine / ThumbnailService / Vulkan / ImGui）。复用
# 与 mikktspace_real_model_test 同款 Avocado fixture + `if(EXISTS)` 门控。helper
# 源（GltfMaterialParse.cpp / MaterialFileIO.cpp）在 tools/OrangeEditor 不属引擎
# lib，直接编进测试 exe（同 material_file_io_test 模式）。cgltf CGLTF_IMPLEMENTATION
# 在测试 TU expand；GltfMaterialParse.cpp 只取声明不 expand，两份不冲突。
if (EXISTS "${_orange_mikkt_fixture}")
    add_executable(gltf_material_import_test
        editor/GltfMaterialImportTest.cpp
        "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfMaterialParse.cpp"
        "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp")
    target_link_libraries(gltf_material_import_test PRIVATE OrangeEngine::orange_engine)
    target_include_directories(gltf_material_import_test PRIVATE
        "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
        "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
        "${CMAKE_SOURCE_DIR}/vendor/cgltf")
    target_compile_definitions(gltf_material_import_test PRIVATE
        "ORANGE_ENGINE_GLTF_FIXTURE=\"${_orange_mikkt_fixture}\"")
    orange_engine_set_compiler_options(gltf_material_import_test)
    add_test(NAME gltf_material_import_test COMMAND gltf_material_import_test)
else ()
    message(STATUS "gltf_material_import_test 跳过：未找到 fixture ${_orange_mikkt_fixture}")
endif ()
# F1 回归（OE-MCP/GUI set_field materialInstance 不视觉生效根因）：
# EnsureMaterialInstance 的 lazy 注册契约。走 EditorAssetContext& seam（绕开
# 无法 headless 链接的 EditorHost）；链 BuiltinAssets + MaterialFileIO +
# ShaderTemplateMetaIO（均无 import-impl/vulkan）。imgui 仅为编译 EditorHost.h
# 间接 include 的 plugin 头提供 include path。
add_executable(material_ensure_lazy_register_test
    editor/MaterialEnsureLazyRegisterTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/BuiltinAssets.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/ShaderTemplateMetaIO.cpp")
target_link_libraries(material_ensure_lazy_register_test PRIVATE
    OrangeEngine::orange_engine OrangeEngine::imgui)
target_include_directories(material_ensure_lazy_register_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(material_ensure_lazy_register_test)
add_test(NAME material_ensure_lazy_register_test COMMAND material_ensure_lazy_register_test)

orange_engine_add_test(scene_world_test         scene/SceneWorldTest.cpp)
orange_engine_add_test(scene_world_partition_test scene/WorldPartitionTest.cpp)
orange_engine_add_test(scene_subtree_clone_test scene/SceneSubtreeCloneTest.cpp)
orange_engine_add_test(scene_serialization_test scene/SceneSerializationTest.cpp)
orange_engine_add_test(scene_entity_guid_test   scene/EntityGuidTest.cpp)
# PIE world-clone 的 EntityGuid 稳定性（A2.3）：复刻 EnterPlay Save(World&) 快照 →
# Stop Load 还原路径，断言 guid 跨 snapshot/restore 稳定 + FindEntityByGuid 命中
# 同一逻辑实体 + Transform/Name/层级一致。纯序列化 + World，走真实磁盘 Save/Load。
orange_engine_add_test(scene_play_snapshot_guid_test scene/PlaySnapshotGuidTest.cpp)
orange_engine_add_test(scene_prefab_test        scene/PrefabTest.cpp)
orange_engine_add_test(prefab_override_test     scene/PrefabOverrideTest.cpp)
orange_engine_add_test(prefab_refresh_test      scene/PrefabRefreshTest.cpp)
orange_engine_add_test(prefab_overridden_paths_test scene/PrefabOverriddenPathsTest.cpp)
# C1.3 prefab apply/revert 引擎层原语（revert 单 leaf 回模板值 + apply 重建模板 blob guid 映回）
orange_engine_add_test(prefab_apply_revert_test scene/prefab_apply_revert_test.cpp)
orange_engine_add_test(scene_transform_system_test scene/TransformSystemTest.cpp)
# local TRS ↔ world matrix 合成/分解（A1 gizmo 写回路径地基；纯数学 header-only）
orange_engine_add_test(transform_math_test       scene/TransformMathTest.cpp)
orange_engine_add_test(post_process_roundtrip_test scene/PostProcessRoundTripTest.cpp)
orange_engine_add_test(scene_file_load_test     scene/SceneFileLoadTest.cpp)
target_compile_definitions(scene_file_load_test PRIVATE
    "ORANGE_ENGINE_REPO_ASSETS_DIR=\"${CMAKE_SOURCE_DIR}/assets\"")

# EditorHierarchy 图操作（tools/OrangeEditor）—— Entity Tree DnD reparent / reorder
# 的底座。逻辑纯 World + HierarchyComponent、零 ImGui/GLFW/Vulkan 依赖，故同
# material_file_io_test 模式：直接把 helper 源编进测试 exe + 加 include path（该源
# 不属引擎 lib）。覆盖编辑器侧 ImGui DnD 交互无法 headless 验证的那层图逻辑。
add_executable(editor_hierarchy_test
    scene/EditorHierarchyTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/EditorHierarchy.cpp")
target_link_libraries(editor_hierarchy_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_hierarchy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_hierarchy_test)
add_test(NAME editor_hierarchy_test COMMAND editor_hierarchy_test)

# EditorPrefabActions（tools/OrangeEditor）—— prefab 编辑器消费 MVP 的命令逻辑
# 层：资产创建 round-trip（SaveSubtreeToString → PrefabLoader::Save → Load）+
# 实例化命令 Execute/Undo/Redo 契约（InstantiatePrefab / DestroySubtree / 再
# InstantiatePrefab 的实体数增删 + shared_ptr 追踪新根）。纯 World + AssetRegistry
# + EditorHierarchy 图操作，零 ImGui/GLFW/Vulkan/EditorHost 依赖，故同
# editor_hierarchy_test 模式：把 EditorHierarchy.cpp 编进测试 exe + 加 include
# path（该源不属引擎 lib）。覆盖编辑器 prefab 消费层无法 headless 验证的 UI 之下
# 那层命令逻辑。
add_executable(editor_prefab_actions_test
    editor/EditorPrefabActionsTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/EditorHierarchy.cpp")
target_link_libraries(editor_prefab_actions_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_prefab_actions_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_prefab_actions_test)
add_test(NAME editor_prefab_actions_test COMMAND editor_prefab_actions_test)

# Headless mesh 导入端到端（GAP-2026-05-27 G1）—— 锁 "资产导入不拉起 GUI" 的
# 解耦链：ImportDispatcher 的 registry-only seam（DispatchToRegistry /
# ImportObjMeshToRegistry / ImportGltfMeshToRegistry）把 .obj / .gltf 导入一个仅
# 注册 Mesh + Texture loader 的最小 AssetRegistry，断言 .mesh + .meta 产出 +
# 可读回 + 确定性。直接编译真实 importer 源（ImportDispatcher / ObjImporter /
# GltfImporter / GltfMaterialParse / MetaSidecar / MeshTangentGen / MaterialFileIO）
# + vendor 单 header（tinyobjloader / cgltf / mikktspace），只链 orange_engine。
# 这些 importer 源 #include EditorHost.h 做 GUI 委托——后者只拉引擎公共头 +
# glm（ThumbnailService 仅前向声明），无 Vulkan/ImGui/GLFW，故 headless 链得通。
# IMPLEMENTATION 宏分布：tinyobjloader 仅 ObjImporter.cpp expand；cgltf 仅
# GltfImporter.cpp expand（GltfMaterialParse.cpp 只取声明），两份不冲突。
# .obj 路径自给 fixture（程序化写立方体），干净 checkout 也跑；.gltf 路径走
# Avocado fixture + ORANGE_ENGINE_GLTF_FIXTURE 注入（缺则编译期短路 SKIP）。
add_executable(headless_mesh_import_test
    editor/HeadlessMeshImportTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ImportDispatcher.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ObjImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxAxisConverter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c")
target_link_libraries(headless_mesh_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(headless_mesh_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX")
if (EXISTS "${_orange_mikkt_fixture}")
    target_compile_definitions(headless_mesh_import_test PRIVATE
        "ORANGE_ENGINE_GLTF_FIXTURE=\"${_orange_mikkt_fixture}\"")
endif ()
orange_engine_set_compiler_options(headless_mesh_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME headless_mesh_import_test COMMAND headless_mesh_import_test)

# glTF 内嵌贴图导入（.glb buffer_view / data: URI）—— 锁 ".glb 内嵌 baseColor PNG
# 被提取成 assets/Models/<stem>/ 下真实磁盘文件 + 回填 .material baseColor 槽" 这条
# 链（GAP-2026-05-25 内嵌贴图子缺口）。自包含：最小内嵌 .glb 以字节数组嵌进测试
# .cpp（由 tests/fixtures/gen_embedded_glb.py --carray 生成），干净 checkout 也跑，
# 不依赖外部 fixture。链接方式同 headless_mesh_import_test：直接编译真实 importer
# 源（ImportDispatcher / ObjImporter / GltfImporter / GltfMaterialParse / MetaSidecar
# / MeshTangentGen / MaterialFileIO）+ vendor 单 header，只链 orange_engine。
add_executable(gltf_embedded_texture_import_test
    editor/GltfEmbeddedTextureImportTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ImportDispatcher.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ObjImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxAxisConverter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c")
target_link_libraries(gltf_embedded_texture_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(gltf_embedded_texture_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX")
orange_engine_set_compiler_options(gltf_embedded_texture_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME gltf_embedded_texture_import_test COMMAND gltf_embedded_texture_import_test)

# glTF scene-level 导入（GAP-2026-05-28 G1）—— 锁 "多 node / 多 mesh .gltf 导入后
# 保留 transform 层级 + 每 mesh 单独 .mesh + 产出可 Scene::Load 的 .scene.json" 这条
# 链（区别于 headless_mesh_import_test 的整文件塌平合并）。自包含 3-node 层级 fixture，
# 干净 checkout 也跑。链接方式同上：GltfSceneImporter.cpp + GltfImporter.cpp（提供
# cgltf IMPLEMENTATION）+ 其余 importer 源 + vendor 单 header，只链 orange_engine。
add_executable(gltf_scene_import_test
    editor/GltfSceneImportTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfSceneImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ImportDispatcher.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ObjImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxAxisConverter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c")
target_link_libraries(gltf_scene_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(gltf_scene_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX")
orange_engine_set_compiler_options(gltf_scene_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME gltf_scene_import_test COMMAND gltf_scene_import_test)

# FBX 静态 mesh + 材质导入端到端 —— 锁 ".fbx → OpenFBX 解析 + 坐标系转换
# （Z-up/cm → 引擎 Y-up/米）+ 多 material 拆 sub-mesh + .mesh/.material/.meta 产出
# + 可读回" 这条链。链接方式同 headless_mesh_import_test：直接编译真实 importer
# 源（FbxImporter / ImportDispatcher / 其余 importer 复用件 / MaterialFileIO）+
# vendor（OpenFBX ofbx.cpp / libdeflate.c + mikktspace.c），只链 orange_engine。
# OpenFBX 不是单 header：ofbx.cpp（C++）+ libdeflate.c（C）作为源文件编进 exe，
# 各 per-TU /W0（vendored 不改、不享 /WX）。fixture（cube_two_material.fbx）经
# ORANGE_ENGINE_FBX_FIXTURE 注入 + if(EXISTS) 门控（缺则编译期 SKIP 几何段，仍测
# ext 路由 + 缺文件路径）。
add_executable(fbx_import_test
    editor/FbxImportTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxAxisConverter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ImportDispatcher.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ObjImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c")
target_link_libraries(fbx_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(fbx_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX")
set(_orange_fbx_fixture "${CMAKE_SOURCE_DIR}/tests/fixtures/cube_two_material.fbx")
if (EXISTS "${_orange_fbx_fixture}")
    target_compile_definitions(fbx_import_test PRIVATE
        "ORANGE_ENGINE_FBX_FIXTURE=\"${_orange_fbx_fixture}\"")
endif ()
orange_engine_set_compiler_options(fbx_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME fbx_import_test COMMAND fbx_import_test)

# FBX scene-level 导入端到端 —— 锁 ".fbx → OpenFBX 解析 + 保留 node 层级 + 每
# mesh node 单独不塌平 + node local transform 经 R·M·R⁻¹ 共轭换轴 + 产出可
# Scene::Load 的 .scene.json" 这条链（区别于 fbx_import_test 的整文件塌平合并）。
# 链接方式同 gltf_scene_import_test：FbxSceneImporter.cpp + 共享 importer 源 +
# vendor（OpenFBX ofbx.cpp / libdeflate.c + mikktspace.c），只链 orange_engine。
# 层级 fixture（cube_hierarchy.fbx，parent + 非平凡 local transform 的 child）经
# ORANGE_ENGINE_FBX_SCENE_FIXTURE 注入 + if(EXISTS) 门控（缺则编译期 SKIP 几何
# 段，仍测 importer 可链 + 缺文件路径）。
add_executable(fbx_scene_import_test
    editor/FbxSceneImportTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxSceneImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxAxisConverter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/FbxMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ImportDispatcher.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/ObjImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfImporter.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/GltfMaterialParse.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c")
target_link_libraries(fbx_scene_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(fbx_scene_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf"
    "${CMAKE_SOURCE_DIR}/vendor/OpenFBX")
set(_orange_fbx_scene_fixture "${CMAKE_SOURCE_DIR}/tests/fixtures/cube_hierarchy.fbx")
if (EXISTS "${_orange_fbx_scene_fixture}")
    target_compile_definitions(fbx_scene_import_test PRIVATE
        "ORANGE_ENGINE_FBX_SCENE_FIXTURE=\"${_orange_fbx_scene_fixture}\"")
endif ()
# 相机 fixture（cube_with_camera.fbx，RefCube mesh + Cam camera node）经
# ORANGE_ENGINE_FBX_CAMERA_FIXTURE 注入 + if(EXISTS) 门控，验 FBX camera node →
# Render::Camera（投影 + 朝向桥接）。与层级 fixture 独立，各自缺失各自 SKIP。
set(_orange_fbx_camera_fixture "${CMAKE_SOURCE_DIR}/tests/fixtures/cube_with_camera.fbx")
if (EXISTS "${_orange_fbx_camera_fixture}")
    target_compile_definitions(fbx_scene_import_test PRIVATE
        "ORANGE_ENGINE_FBX_CAMERA_FIXTURE=\"${_orange_fbx_camera_fixture}\"")
endif ()
orange_engine_set_compiler_options(fbx_scene_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/ofbx.cpp"
        "${CMAKE_SOURCE_DIR}/vendor/OpenFBX/libdeflate.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME fbx_scene_import_test COMMAND fbx_scene_import_test)

# CommandStack（tools/OrangeEditor）—— Undo/Redo 栈 + coalesce/group + Undo-label
# peek。纯 ICommand + std，零 ImGui/GLFW/Vulkan 依赖，故同 editor_hierarchy_test
# 模式：把 CommandStack.cpp 编进测试 exe + 加 include path（该源不属引擎 lib）。
add_executable(command_stack_test
    command/CommandStackTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/command/CommandStack.cpp")
target_link_libraries(command_stack_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(command_stack_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(command_stack_test)
add_test(NAME command_stack_test COMMAND command_stack_test)

# EditorSettings（tools/OrangeEditor）—— JSON 序列化往返 + 前向兼容（缺字段走
# 默认）。helper 源不属引擎 lib，直接编进测试 exe + 加 include path（同
# material_file_io_test 模式）。仅依赖 Core::Serialization + glm，零 ImGui。
add_executable(editor_settings_test
    editor/EditorSettingsTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/context/EditorSettings.cpp")
target_link_libraries(editor_settings_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_settings_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_settings_test)
add_test(NAME editor_settings_test COMMAND editor_settings_test)

# EditorTextUtil（tools/OrangeEditor）—— header-only Util::ContainsCaseInsensitive
# （Console + Asset 搜索共用谓词）。纯 std 头，仅需 include path（无 helper .cpp）。
add_executable(editor_text_util_test editor/EditorTextUtilTest.cpp)
target_link_libraries(editor_text_util_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_text_util_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_text_util_test)
add_test(NAME editor_text_util_test COMMAND editor_text_util_test)

# EditorMathUtil（tools/OrangeEditor）—— header-only Util::SnapToStep（gizmo snap
# 量化核心）。纯 std 头，仅需 include path。
add_executable(editor_math_util_test editor/EditorMathUtilTest.cpp)
target_link_libraries(editor_math_util_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_math_util_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_math_util_test)
add_test(NAME editor_math_util_test COMMAND editor_math_util_test)

# EditorGroupTransform（tools/OrangeEditor）—— header-only 多选群组变换 pivot
# 数学基元（绕 pivot rotate/scale + 质心）。仅需 glm（经 orange_engine PUBLIC
# 传递）+ include path，无 helper .cpp。同 editor_math_util_test 模式。
add_executable(editor_group_transform_test editor/EditorGroupTransformTest.cpp)
target_link_libraries(editor_group_transform_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_group_transform_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_group_transform_test)
add_test(NAME editor_group_transform_test COMMAND editor_group_transform_test)

# EditorGizmoMath（tools/OrangeEditor）—— gizmo 反投影 / 最近点 / ray-plane /
# 屏幕投影数学（translate/rotate/scale 三 gizmo 拖动+hit-test 地基）。纯 glm，
# helper 源不属引擎 lib，编进测试 exe + include path（同 editor_hierarchy_test）。
add_executable(editor_gizmo_math_test
    editor/EditorGizmoMathTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/EditorGizmoMath.cpp")
target_link_libraries(editor_gizmo_math_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(editor_gizmo_math_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(editor_gizmo_math_test)
add_test(NAME editor_gizmo_math_test COMMAND editor_gizmo_math_test)
orange_engine_add_test(save_game_registry_test  save/SaveGameRegistryTest.cpp)
orange_engine_add_test(save_game_system_test    save/SaveGameSystemTest.cpp)
orange_engine_add_test(save_path_test           save/SavePathTest.cpp)
orange_engine_add_test(slot_manager_test        save/SlotManagerTest.cpp)
orange_engine_add_test(autosave_scheduler_test  save/AutosaveSchedulerTest.cpp)
orange_engine_add_test(save_game_migrator_test  save/SaveGameMigratorTest.cpp)
orange_engine_add_test(save_corruption_test     save/SaveCorruptionTest.cpp)
orange_engine_add_test(render_interface_test    render/RenderInterfaceTest.cpp)
orange_engine_add_test(render_scene_test        render/RenderSceneTest.cpp)
orange_engine_add_test(material_interface_test  render/MaterialInterfaceTest.cpp)
orange_engine_add_test(builtin_materials_test   render/BuiltinMaterialsTest.cpp)
orange_engine_add_test(postprocess_chain_test   render/PostProcessChainTest.cpp)
orange_engine_add_test(material_system_test     render/MaterialSystemTest.cpp)
# REPO_ASSETS_DIR：让 parity 测试加载真实 assets/shaders/templates/ 与
# RegisterBuiltins 内置路径对比 usesTangentVertex（数据驱动模板漂移防护）。
target_compile_definitions(material_system_test PRIVATE
    "ORANGE_ENGINE_REPO_ASSETS_DIR=\"${CMAKE_SOURCE_DIR}/assets\"")
orange_engine_add_test(light_and_shadow_test    render/LightAndShadowTest.cpp)
orange_engine_add_test(material_schema_test     render/MaterialSchemaTest.cpp)
orange_engine_add_test(pipeline_template_cache_test render/PipelineTemplateCacheTest.cpp)
orange_engine_add_test(pipeline_hdr_target_test    render/PipelineHdrTargetTest.cpp)
orange_engine_add_test(pipeline_offscreen_test     render/PipelineOffscreenTest.cpp)
orange_engine_add_test(pipeline_halo_conditional_test render/PipelineHaloConditionalTest.cpp)
orange_engine_add_test(pipeline_render_to_texture_test render/PipelineRenderToTextureTest.cpp)

# 视觉 golden 回归：程序化 PBR 场景 → RenderToTexture → 与参考图逐像素比对。
# 手写 add_executable（带第二源 golden/GoldenImage.cpp，含 PPM codec/Compare
# 实现），但只链 orange_engine（RenderToTexture 全在引擎 lib + 传递的 OR 内，
# 无需编辑器/ImGui/glfw）。goldenDir 作为 argv[1] 传入（= tests/golden/data）。
add_executable(visual_golden_pbr_test
    render/VisualGoldenPbrTest.cpp
    golden/GoldenImage.cpp)
target_link_libraries(visual_golden_pbr_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(visual_golden_pbr_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
orange_engine_set_compiler_options(visual_golden_pbr_test)
add_test(NAME visual_golden_pbr_test
         COMMAND visual_golden_pbr_test "${CMAKE_CURRENT_SOURCE_DIR}/golden/data")
orange_engine_add_test(material_thumbnail_bake_test render/MaterialThumbnailBakeTest.cpp)
orange_engine_add_test(prefab_thumbnail_bake_test  render/PrefabThumbnailBakeTest.cpp)
orange_engine_add_test(mesh_thumbnail_bake_test    render/MeshThumbnailBakeTest.cpp)
orange_engine_add_test(scene_thumbnail_bake_test   render/SceneThumbnailBakeTest.cpp)
orange_engine_add_test(pbr_scene_black_repro_test  render/PbrSceneBlackReproTest.cpp)
target_compile_definitions(pbr_scene_black_repro_test PRIVATE
    "ORANGE_ENGINE_REPO_ASSETS_DIR=\"${CMAKE_SOURCE_DIR}/assets\"")
orange_engine_add_test(shadow_occlusion_test       render/ShadowOcclusionTest.cpp)
orange_engine_add_test(bloom_chain_test            render/BloomChainTest.cpp)
orange_engine_add_test(vfx_system_test             render/VfxSystemTest.cpp)
orange_engine_add_test(insert_pass_test            render/InsertPassTest.cpp)

# Material：.material 文件 v1.1 schema helper（MaterialFileIO）。
# 不走 orange_engine_add_test：helper 源码在 tools/OrangeEditor/ 不属于引擎 lib，
# 测试 exe 直接编译它 + 加 include path 取 helper 头。本测试 standalone build
# 时不消费 OrangeEditor 任何 ImGui / GLFW / Vulkan 路径——纯引擎公共面 + helper。
add_executable(material_file_io_test
    render/MaterialFileIOTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp")
target_link_libraries(material_file_io_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(material_file_io_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(material_file_io_test)
add_test(NAME material_file_io_test COMMAND material_file_io_test)

# Animation：FSM + Backend Registry（Phase 4 / Task 01 公共接口）
# + ProceduralAnimator 后端（Task 04，shader-uniform 驱动）
orange_engine_add_test(animation_state_machine_test animation/AnimationStateMachineTest.cpp)
orange_engine_add_test(animator_registry_test       animation/AnimatorRegistryTest.cpp)
orange_engine_add_test(procedural_animator_test     animation/ProceduralAnimatorTest.cpp)
# AnimationClip 数据模型 + 采样（B2.1 地基，纯数据 + header-only 采样）
orange_engine_add_test(animation_clip_test          animation/AnimationClipTest.cpp)
# ClipAnimator：AnimationClip → 实体 TransformComponent 的运行时消费（B2.2）
orange_engine_add_test(clip_animator_test           animation/ClipAnimatorTest.cpp)
# BlendSpace：参数化姿势混合（blend space / blend tree）—— N-pose 混合数学 +
# 1D/2D 求值 + additive 层叠 + BlendSpaceAnimator 后端（纯 CPU header-only 采样，
# headless 可完整验证混合数学）。
orange_engine_add_test(blend_space_test             animation/BlendSpaceTest.cpp)
# AnimationClip .anim JSON 序列化 round-trip（B2.2，走 Core::Serialization）
orange_engine_add_test(animation_clip_serialization_test animation/AnimationClipSerializationTest.cpp)
# AnimationClipLoader：.anim 经 AssetRegistry 加载（B2.2）
orange_engine_add_test(animation_clip_loader_test   animation/AnimationClipLoaderTest.cpp)
# ClipAnimator clip 重指派：Inspector clipSet 序列的纯数据语义（B2.6）
orange_engine_add_test(clip_animator_asset_reassign_test animation/ClipAnimatorAssetReassignTest.cpp)
# Timeline / dopesheet 编辑数据层 round-trip（B2.3）：SetAnimationClipCommand
# 等价的 do/undo 对称 + clip CRUD 原语 + .anim 写回保真（纯数据，不链编辑器 TU）
orange_engine_add_test(timeline_edit_primitives_test animation/TimelineEditPrimitivesTest.cpp)
# 曲线编辑器切线编辑数据层（B2.4）：手柄屏幕落点 ↔ 切线逆/正运算 round-trip +
# 改 inTangent/outTangent → SampleTrack 值按预期变 + do/undo 对称（纯数据，不链编辑器 TU）
orange_engine_add_test(curve_editor_primitives_test animation/CurveEditorPrimitivesTest.cpp)
# AnimFsmFileIO round-trip（B2.5 状态机图）—— 锁 .anim_fsm 读写对称 + 节点画布
# 坐标（editorPos = state.layout）持久化 + 旧文件缺 layout 向后兼容。AnimFsmFileIO.cpp
# helper 源在 tools/OrangeEditor 不属引擎 lib，直接编进测试 exe + 加 include path
# （同 material_file_io_test 模式）。纯 Core::Serialization + AnimFsmModel POD，零
# ImGui/GLFW/Vulkan；节点图所有 ImGui 交互须 dogfood，此测试只覆盖 IO seam。
add_executable(anim_fsm_file_io_test
    editor/AnimFsmFileIORoundTripTest.cpp
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/AnimFsmFileIO.cpp")
target_link_libraries(anim_fsm_file_io_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(anim_fsm_file_io_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor")
orange_engine_set_compiler_options(anim_fsm_file_io_test)
add_test(NAME anim_fsm_file_io_test COMMAND anim_fsm_file_io_test)

# Animation：DragonBones runtime 接入 smoke（Phase 4 / Task 02）。
# 这一档不走 orange_engine_add_test：它需要把 src/ 加进 include path
# 才能 include "animation/dragonbones/DragonBonesContext.h"——后者是
# 引擎内部头（不在公共 include/ 下）。CLAUDE.md "Header isolation" 仍
# 然成立：该测试**不**直接 #include <dragonBones/...>，runtime 头只
# 通过 DragonBonesContext.cpp 单点引用。
add_executable(dragonbones_runtime_smoke animation/DragonBonesRuntimeSmoke.cpp)
target_link_libraries(dragonbones_runtime_smoke PRIVATE OrangeEngine::orange_engine)
target_include_directories(dragonbones_runtime_smoke PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
orange_engine_set_compiler_options(dragonbones_runtime_smoke)
add_test(NAME dragonbones_runtime_smoke COMMAND dragonbones_runtime_smoke)

# Animation：SkeletalAnimator + SkeletonLoader 端到端（Phase 4 / Task 03）。
# 测试用 vendor/DragonBones/Cocos2DX_3.x/Demos/Resources/bullet_01/ 内
# 的 .json 作为已知曲线 fixture——通过 ORANGE_ENGINE_TEST_DATA_DIR 编
# 译期注入路径。同样需要 src/ include 才能拿 DragonBonesContext。
add_executable(skeletal_animator_test animation/SkeletalAnimatorTest.cpp)
target_link_libraries(skeletal_animator_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(skeletal_animator_test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_compile_definitions(skeletal_animator_test PRIVATE
    "ORANGE_ENGINE_TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/vendor/DragonBones/Cocos2DX_3.x/Demos/Resources/bullet_01\""
)
orange_engine_set_compiler_options(skeletal_animator_test)
add_test(NAME skeletal_animator_test COMMAND skeletal_animator_test)

# Physics：World / RigidBody / Collider 公共接口（Phase 4 / Task 05）+
# Box2D 后端步进语义验收（Phase 4 / Task 06，自由落体 / 地面碰撞 / 写读 transform）
# + 运行时 collider 替换（Phase 4 / Task 07，半径 1 → 0.3 mass / 静止位置都跟变）
orange_engine_add_test(physics_interface_test       physics/PhysicsInterfaceTest.cpp)
orange_engine_add_test(box2d_step_test              physics/Box2DStepTest.cpp)
orange_engine_add_test(collider_shape_fall_test     physics/ColliderShapeFallTest.cpp)
orange_engine_add_test(fixture_replace_test         physics/FixtureReplaceTest.cpp)
orange_engine_add_test(physics_query_test           physics/PhysicsQueryTest.cpp)
# collision filtering（collider category/mask 位 + 查询 QueryFilter）：碰撞过滤 /
# 查询过滤 / 序列化 round-trip + 向后兼容。向后兼容子测试直接调内部 seam
# ReadColliderDesc，故额外加 src include path（同 scene_file_load_test 模式）。
orange_engine_add_test(collision_filter_test        physics/CollisionFilterTest.cpp)
target_include_directories(collision_filter_test PRIVATE "${CMAKE_SOURCE_DIR}/src")

# Nav：2D 网格导航 —— NavGrid helper 一致性 + A* 寻路正确性（直线 / 绕障 /
# 无路 / 退化 / 对角禁切角）+ SimplifyPath LOS 平滑 + BakeWalkableFromPhysics
# 从 static box collider 烘焙可行走性。纯 orange_engine（NavGrid + Pathfinding
# + PhysicsWorld），headless 完全可验。
orange_engine_add_test(navigation_test              nav/NavigationTest.cpp)

# Spline：样条 / 曲线求值（Catmull-Rom 过控制点 + Linear）+ tangent + 弧长
# 参数化匀速遍历（BuildSplineArcTable / EvaluateSplineByDistance）+ 闭合环绕 +
# 退化输入。纯数学 headless 完全可验（仅 orange_engine + glm，无 Vulkan/GUI）。
orange_engine_add_test(spline_test                  spline/SplineTest.cpp)

# Tilemap：瓦片数据模型 helper（MakeTileMap / SetTile-GetTile / IsSolidTile 边界
# / TileCenterToWorld ↔ WorldToTile）+ 贪心碰撞矩形合并（MergeSolidTiles 覆盖完
# 整性：每实心 tile 恰一 rect / 无 rect 盖非实心 / 面积和守恒）+ TileRectToWorldAABB
# + 从 tilemap 烘焙 NavGrid（BuildNavGridFromTileMap 实心→不可走 + 网格对齐）。
# 纯数据 + 算法 headless 完全可验（仅 orange_engine + glm，无 Vulkan/GUI）。
orange_engine_add_test(tilemap_test                 tilemap/TileMapTest.cpp)

# Noise：程序化生成基础 —— seeded PRNG（splitmix64 确定性 + 范围 / 退化）+ value
# noise（hash 格点 + 五次 fade 双线性，[0,1] + 连续性）+ Perlin gradient noise 2D/3D
# （梯度选择 + fade 插值，约 [-1,1] + 整数格点 ≈0 性质 + 连续性）+ fBm 分形（octaves
# 叠加 + 总振幅归一化 + octaves=1 退化回 Perlin + 多 octaves 增细节）。纯确定性
# scalar 数学 headless 完全可验（仅 orange_engine + 标准库，无 glm/Vulkan/GUI）。
orange_engine_add_test(noise_test                   noise/NoiseTest.cpp)

# Tween：缓动补间（tween / easing）—— 代码驱动的标量插值运行时。覆盖 31 条
# easings.net 缓动函数（端点严格 0/1 + t clamp + 已知值 + 单调 / InOut 中点 0.5）
# + TweenManager 运行时（Once/Loop/PingPong 循环语义 + delay + onUpdate/onComplete
# 回调 + Kill/Clear + 抗再入 Update + 除零守卫 + 不可拷贝/移动 static_assert）。纯
# 确定性 scalar 数学 headless 完全可验（仅 orange_engine + 标准库，无 glm/Vulkan/GUI）。
orange_engine_add_test(tween_test                   tween/TweenTest.cpp)

# Spatial：均匀网格空间哈希（uniform-grid spatial hash / broad-phase）—— 引擎首个
# 空间加速结构。覆盖基本 Insert/QueryAABB + 多格 item dedup + QueryPoint + QueryRadius
# （圆 vs AABB narrow）+ 负坐标（CellCoord floor）+ Remove（摘 cell 干净）+ Update/移动
# + 边界接触（<= 语义）+ Clear/空 + cellSize<=0 钳制 + ★暴力法交叉验证（N=300 item /
# M=50 query 与暴力扫描逐一相等，验完备性 + narrow 正确性 + dedup）。纯数据结构 + 几何
# headless 完全可验（仅 orange_engine + glm，无 Vulkan/GUI）。
orange_engine_add_test(spatial_hash_test            spatial/SpatialHashTest.cpp)

# CameraRig：2D 相机跟随控制器（deadzone + 临界阻尼 SmoothDamp + 世界边界钳制）。覆盖瞬移
# 模式 + 死区保持/跟随（贴边沿）+ SmoothDamp 收敛（单调逼近 / 不过冲）+ maxSpeed 钳速 +
# 边界钳制（含某轴 min>max 退化）+ SetPosition 清速度 + dt<=0 不动。纯数据 + 数学，仅
# orange_engine + glm，headless 完全可验（无 Vulkan/GUI）。
orange_engine_add_test(camera_follow_test           camerarig/CameraFollowTest.cpp)

# CameraShake：基于 trauma 的 2D 相机抖动（juice 原语）。覆盖 AddTrauma/SetTrauma 钳制 +
# trauma 线性衰减到 0 + 零 trauma 零偏移 + 偏移有界（<=maxTranslation）+ 满 trauma 有实际
# 抖动量 + 强度随 trauma 单调 + 同 seed+dt 确定性 + maxRoll=0 时 roll 恒 0。自包含 hash
# 值噪声，仅 orange_engine + glm，headless 完全可验。
orange_engine_add_test(camera_shake_test            camerarig/CameraShakeTest.cpp)

# CameraZoom：平滑相机缩放（临界阻尼 SmoothDamp + zoom 钳制）——相机工具三件套第三件。
# 覆盖钳制 [min,max] + SmoothDamp 收敛（单调/不过冲）+ 瞬移模式 + maxSpeed 钳速 + min>max
# 退化 + SetZoom 清速度同步目标 + dt<=0 不动。header-only 纯数学，仅 orange_engine + glm。
orange_engine_add_test(camera_zoom_test             camerarig/CameraZoomTest.cpp)

# CameraRig：follow+shake+zoom 组合装置。覆盖无 shake 时 position==follow + shake 只叠视觉
# 偏移不污染 follow 真相（GetFollowPosition 恒==target）+ 偏移有界 + zoom 合成 + 便利转发
# （AddTrauma/SetTargetZoom）。header-only 纯聚合，仅 orange_engine + glm。
orange_engine_add_test(camera_rig_test              camerarig/CameraRigTest.cpp)

# Particle：2D CPU 粒子发射器 + 仿真核（juice 原语，flow 主角喷溅 / 拖尾 / 尘屑）。
# header-only（复用 Noise::Rng 确定性 + Tween::Easing size-over-life），无 .cpp 不进
# add_library，由测试 TU 实例化。覆盖 Particle 查询（NormalizedAge/SizeAt/ColorAt/
# lifetime<=0 守卫）+ Emit 溢出丢弃 + ★确定性逐粒子复现（穿越大量死亡）+ 寿命淘汰 +
# swap-remove 压缩身份正确 + 半隐式欧拉重力/阻尼积分 + emitRate 小数余量帧率无关 +
# Disk/Box/Point 形状包含 + spread0 方向速度 + dt<=0 不动 + Clear。仅 orange_engine +
# glm + 标准库，headless 完全可验（无 Vulkan/GUI）。
orange_engine_add_test(particle_system_test         particle/ParticleSystemTest.cpp)

# Steering：2D 转向行为库（Reynolds seek/flee/arrive/pursue/evade/wander + 积分 Agent）——
# 局部自主运动原语，与 nav（全局 A* 寻路）分工。header-only 纯 glm+数学（Wander 随机复用
# Noise::Rng 确定性）。覆盖 Truncate 钳制 + Seek 力向/闭环收敛 + Flee 背离 + ★Arrive 到点
# 平滑刹停（末速≈0，对比 Seek 恒全速）+ 减速带力 < Seek + Pursue 预测拦截（力瞄前方）+
# Wander 同 seed 确定性/有界/angle 扰动 + Agent maxSpeed 钳速/dt<=0 不动。零向量 Normalize
# 守卫无 NaN。仅 orange_engine + glm，headless 完全可验。
orange_engine_add_test(steering_test                steering/SteeringTest.cpp)

# Timer：gameplay 倒计时 / 冷却原语（CountdownTimer one-shot/looping + Cooldown 门）。
# header-only 纯 scalar 逻辑无依赖。覆盖 one-shot 触发一次即完成 + Reset + Progress 精确 +
# looping 补周期（★单次大 dt 跨多周期触发 >1）+ Stop + duration<=0 退化 no-op（★不无限
# 补周期）+ Cooldown TryTrigger 门控 / Tick 消耗钳 0 / FractionReady / ResetReady /
# dt<=0 不动 / duration<=0 恒就绪。仅 orange_engine + 标准库，headless 完全可验。
orange_engine_add_test(timer_test                   timer/TimerTest.cpp)

# RingBuffer：固定容量环形缓冲（最近 N 项历史，输入缓冲 / 拖尾轨迹）。header-only 模板纯
# 数据结构。覆盖初始空 + 未满 Push 最新优先下标 + Newest/Oldest + 满后★环形覆盖最旧下标
# 仍正确 + Clear 保留容量 + 容量 0 退化 no-op。仅 orange_engine + 标准库，headless 可验。
orange_engine_add_test(ring_buffer_test             container/RingBufferTest.cpp)

# Fsm：通用逻辑状态机（gameplay / AI finite state machine）—— 可复用行为原语。
# 覆盖事件转移 + 回调序（onExit 先于 onEnter）/ guard（假不转、真转、多条取第一条
# 通过的）/ 自动转移（每次 Update 至多一次 + onUpdate 每次被调）/ onUpdate 里 Fire /
# ★抗再入链式转移（onEnter 里 Fire 走 pending 队列非递归）/ Start-Stop（含重复 Start
# 先停旧态）/ IsIn/IsRunning/Current + 未注册状态安全 + 编译期不可拷贝/移动断言。
# 纯逻辑（仅 orange_engine + 标准库，无 glm/Vulkan/GUI），headless 完全可验。
orange_engine_add_test(state_machine_test           fsm/StateMachineTest.cpp)

# Event：类型化事件总线（EventBus / typed pub-sub）—— 解耦通信的基础引擎服务。
# 纯 header-only + 类型擦除模板（无 .cpp、不进 add_library），由测试 TU 实例化。
# 覆盖发布订阅 / 多处理器顺序 / 类型隔离 / Unsubscribe / 延迟队列 FIFO / 再入
# 安全（快照迭代）/ Clear。仅 orange_engine + 标准库，headless 完全可验。
orange_engine_add_test(event_bus_test               event/EventBusTest.cpp)

# Input：ActionMap 状态机 + JSON 加载 + InputContext 栈语义（Phase 4 / Task 08）
orange_engine_add_test(action_map_test         input/ActionMapTest.cpp)
orange_engine_add_test(input_context_stack_test input/InputContextStackTest.cpp)
target_compile_definitions(action_map_test PRIVATE
    "ORANGE_ENGINE_INPUT_TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/assets/configs\""
)

# Audio：AudioEngine init/shutdown smoke + SoundInstance 边界（Phase 4 / Task 09）
orange_engine_add_test(audio_engine_smoke_test  audio/AudioEngineSmokeTest.cpp)

# ---------------------------------------------------------------------------
# Script：CoreCLR host spike（ADR-017 B1.0）—— 仅 ORANGE_ENGINE_WITH_DOTNET=ON 加入
#
# 证明 C++ host 初始化 .NET 运行时 → 加载托管 probe assembly → 取
# [UnmanagedCallersOnly] 方法函数指针 → 调用断言返回值 + 错误路径不崩。
#
# fixture 构建：用 dotnet build 出 HostProbe.dll + HostProbe.runtimeconfig.json
# 到 csharp/HostProbe/bin/<Config>/net8.0/。custom target 串成测试的构建依赖；
# 产物绝对路径经 target_compile_definitions 注入测试 TU。
# ---------------------------------------------------------------------------
if (ORANGE_ENGINE_WITH_DOTNET)
    find_program(ORANGE_ENGINE_DOTNET_EXE NAMES dotnet
        HINTS "$ENV{DOTNET_ROOT}" "C:/Program Files/dotnet")
    if (NOT ORANGE_ENGINE_DOTNET_EXE)
        message(FATAL_ERROR
            "OrangeEngine: ORANGE_ENGINE_WITH_DOTNET=ON 但 PATH / DOTNET_ROOT 下没找到 dotnet。"
            " dotnet_host_test 需要它构建托管 probe fixture。")
    endif ()

    set(_orange_probe_proj
        "${CMAKE_SOURCE_DIR}/csharp/HostProbe/HostProbe.csproj")
    # dotnet build -c <cfg> 落到 bin/<cfg>/net8.0/。ctest -C 的配置经 $<CONFIG>
    # 传进来；single-config 生成器下 $<CONFIG> 多为空，这里仍按显式配置目录走。
    set(_orange_probe_out_dir
        "${CMAKE_SOURCE_DIR}/csharp/HostProbe/bin/$<CONFIG>/net8.0")
    set(_orange_probe_dll "${_orange_probe_out_dir}/HostProbe.dll")
    set(_orange_probe_cfg "${_orange_probe_out_dir}/HostProbe.runtimeconfig.json")

    # custom command：用 dotnet build 产出 probe 程序集。OUTPUT 用 .dll 让
    # 增量构建可跳过；DEPENDS 源 .cs / .csproj 触发重建。
    add_custom_command(
        OUTPUT  "${_orange_probe_dll}" "${_orange_probe_cfg}"
        COMMAND "${ORANGE_ENGINE_DOTNET_EXE}" build "${_orange_probe_proj}"
                -c $<CONFIG> --nologo -v quiet
        DEPENDS "${CMAKE_SOURCE_DIR}/csharp/HostProbe/Probe.cs"
                "${_orange_probe_proj}"
        COMMENT "OrangeEngine: 构建托管 probe fixture（HostProbe.dll + runtimeconfig）"
        VERBATIM)
    add_custom_target(orange_engine_host_probe ALL
        DEPENDS "${_orange_probe_dll}" "${_orange_probe_cfg}")

    add_executable(dotnet_host_test script/dotnet_host_test.cpp)
    add_dependencies(dotnet_host_test orange_engine_host_probe)
    target_link_libraries(dotnet_host_test PRIVATE OrangeEngine::orange_engine)
    target_compile_definitions(dotnet_host_test PRIVATE
        "ORANGE_PROBE_ASSEMBLY_PATH=\"${_orange_probe_dll}\""
        "ORANGE_PROBE_RUNTIMECONFIG_PATH=\"${_orange_probe_cfg}\"")
    orange_engine_set_compiler_options(dotnet_host_test)
    # nethost.dll 必须在测试 exe 的 DLL 搜索路径上（get_hostfxr_path 经它解析）。
    # 引擎链 import 库 nethost.lib，运行期 DLL 由此 post-build 拷到 exe 目录。
    add_custom_command(TARGET dotnet_host_test POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ORANGE_ENGINE_DOTNET_NETHOST_DLL}"
                "$<TARGET_FILE_DIR:dotnet_host_test>"
        VERBATIM)
    add_test(NAME dotnet_host_test COMMAND dotnet_host_test)

    # -----------------------------------------------------------------------
    # script_runtime_test（ADR-017 B1.1）—— C# 脚本经函数指针表操作引擎实体的
    # 端到端验证。建 World + 实体 → ScriptRuntime 起 CoreCLR + 取托管 glue +
    # Bootstrap 推绑定表 → 实例化 OrangeFixtures.Mover → OnStart/OnUpdate →
    # 脚本 Entity.Position += (1,0,0) 回调进 C++ → 断言 TransformComponent
    # 真变了 + Entity_IsValid 正确。
    #
    # fixture：dotnet build ScriptFixtures.csproj（ProjectReference 到
    # OrangeScriptSDK，会顺带编出 OrangeScriptSDK.dll 到同 bin 目录）产出
    # ScriptFixtures.dll + ScriptFixtures.runtimeconfig.json + OrangeScriptSDK.dll。
    # 三者绝对路径经 target_compile_definitions 注入测试 TU。
    # -----------------------------------------------------------------------
    set(_orange_fixtures_proj
        "${CMAKE_SOURCE_DIR}/csharp/ScriptFixtures/ScriptFixtures.csproj")
    set(_orange_fixtures_out_dir
        "${CMAKE_SOURCE_DIR}/csharp/ScriptFixtures/bin/$<CONFIG>/net8.0")
    set(_orange_fixtures_dll "${_orange_fixtures_out_dir}/ScriptFixtures.dll")
    set(_orange_fixtures_cfg "${_orange_fixtures_out_dir}/ScriptFixtures.runtimeconfig.json")
    set(_orange_fixtures_sdk_dll "${_orange_fixtures_out_dir}/OrangeScriptSDK.dll")

    add_custom_command(
        OUTPUT  "${_orange_fixtures_dll}" "${_orange_fixtures_cfg}" "${_orange_fixtures_sdk_dll}"
        COMMAND "${ORANGE_ENGINE_DOTNET_EXE}" build "${_orange_fixtures_proj}"
                -c $<CONFIG> --nologo -v quiet
        DEPENDS "${CMAKE_SOURCE_DIR}/csharp/ScriptFixtures/Mover.cs"
                "${CMAKE_SOURCE_DIR}/csharp/ScriptFixtures/Tweakable.cs"
                "${_orange_fixtures_proj}"
                "${CMAKE_SOURCE_DIR}/csharp/OrangeScriptSDK/ScriptRuntime.cs"
                "${CMAKE_SOURCE_DIR}/csharp/OrangeScriptSDK/EngineInterop.cs"
                "${CMAKE_SOURCE_DIR}/csharp/OrangeScriptSDK/Entity.cs"
                "${CMAKE_SOURCE_DIR}/csharp/OrangeScriptSDK/OrangeScript.cs"
                "${CMAKE_SOURCE_DIR}/csharp/OrangeScriptSDK/OrangeScriptSDK.csproj"
        COMMENT "OrangeEngine: 构建托管脚本 fixture（ScriptFixtures + OrangeScriptSDK glue）"
        VERBATIM)
    add_custom_target(orange_engine_script_fixtures ALL
        DEPENDS "${_orange_fixtures_dll}" "${_orange_fixtures_cfg}" "${_orange_fixtures_sdk_dll}")

    add_executable(script_runtime_test script/script_runtime_test.cpp)
    add_dependencies(script_runtime_test orange_engine_script_fixtures)
    target_link_libraries(script_runtime_test PRIVATE OrangeEngine::orange_engine)
    # src/script/dotnet include 路径取 ScriptBindings.h（纯 C ABI 声明，无 CLR
    # 头）——测试直接验 native 绑定函数 Entity_IsValid 等。
    target_include_directories(script_runtime_test PRIVATE
        "${CMAKE_SOURCE_DIR}/src/script/dotnet")
    target_compile_definitions(script_runtime_test PRIVATE
        "ORANGE_SCRIPTFIXTURES_ASSEMBLY_PATH=\"${_orange_fixtures_dll}\""
        "ORANGE_SCRIPTFIXTURES_RUNTIMECONFIG_PATH=\"${_orange_fixtures_cfg}\""
        "ORANGE_SCRIPT_SDK_ASSEMBLY_PATH=\"${_orange_fixtures_sdk_dll}\"")
    orange_engine_set_compiler_options(script_runtime_test)
    # nethost.dll 必须在测试 exe 的 DLL 搜索路径上（get_hostfxr_path 经它解析）。
    add_custom_command(TARGET script_runtime_test POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ORANGE_ENGINE_DOTNET_NETHOST_DLL}"
                "$<TARGET_FILE_DIR:script_runtime_test>"
        VERBATIM)
    add_test(NAME script_runtime_test COMMAND script_runtime_test)

    # -----------------------------------------------------------------------
    # script_system_test（ADR-017 B1.2）—— ScriptComponent + ScriptSystem 的
    # 批量实例化 + 生命周期 tick 端到端验证。建 World（实体带 Transform 在原点
    # + ScriptComponent 指向 ScriptFixtures 的 Mover）→ ScriptRuntime.Initialize
    # → ScriptSystem.StartWorld（批量 CreateInstance + OnStart）→ Tick ×2 →
    # StopWorld（OnDestroy + Release）→ 断言引擎侧 Transform：y==100 / x==2 /
    # z==7（OnStart / 两次 Tick / OnDestroy 各被调对应次数）。
    #
    # 复用 script_runtime_test 同款 fixture（orange_engine_script_fixtures 产出
    # ScriptFixtures.dll / runtimeconfig + OrangeScriptSDK.dll），三者绝对路径经
    # target_compile_definitions 注入。进程内 CoreCLR 单次启动约束同 runtime
    # 测试：本测试只一次真 ScriptRuntime.Initialize。
    # -----------------------------------------------------------------------
    add_executable(script_system_test script/script_system_test.cpp)
    add_dependencies(script_system_test orange_engine_script_fixtures)
    target_link_libraries(script_system_test PRIVATE OrangeEngine::orange_engine)
    target_compile_definitions(script_system_test PRIVATE
        "ORANGE_SCRIPTFIXTURES_ASSEMBLY_PATH=\"${_orange_fixtures_dll}\""
        "ORANGE_SCRIPTFIXTURES_RUNTIMECONFIG_PATH=\"${_orange_fixtures_cfg}\""
        "ORANGE_SCRIPT_SDK_ASSEMBLY_PATH=\"${_orange_fixtures_sdk_dll}\"")
    orange_engine_set_compiler_options(script_system_test)
    # nethost.dll 必须在测试 exe 的 DLL 搜索路径上（get_hostfxr_path 经它解析）。
    add_custom_command(TARGET script_system_test POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${ORANGE_ENGINE_DOTNET_NETHOST_DLL}"
                "$<TARGET_FILE_DIR:script_system_test>"
        VERBATIM)
    add_test(NAME script_system_test COMMAND script_system_test)
endif ()

# ---------------------------------------------------------------------------
# install_smoke / config_smoke
#
# 仅在引擎自身要发 install 规则时（即 ORANGE_ENGINE_INSTALL=ON）才注
# 册：作为 add_subdirectory 嵌入的父工程会把 ORANGE_ENGINE_INSTALL
# 关掉，那时引擎装不出 SDK，这两个 case 也就无意义。
# ---------------------------------------------------------------------------
if (ORANGE_ENGINE_INSTALL)
    add_test(
        NAME install_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DORANGE_ENGINE_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DORANGE_ENGINE_INSTALL_DIR=${CMAKE_BINARY_DIR}/install_smoke"
            "-DORANGE_ENGINE_CONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install/install_smoke.cmake"
    )

    # config_smoke 把已经在父 build 里解析到的依赖 config 路径整套转
    # 发给消费者 cmake：消费者无需重新搜索就能把 find_dependency 链
    # 全部走通。Vulkan 走 VULKAN_SDK 环境变量自解析，CMAKE_PREFIX_PATH
    # 同时把父 build 的搜索前缀（默认 D:/3rdparty/install）继承过去。
    add_test(
        NAME config_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DORANGE_ENGINE_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DORANGE_ENGINE_INSTALL_DIR=${CMAKE_BINARY_DIR}/config_smoke_install"
            "-DORANGE_ENGINE_CONSUMER_SRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}/install/config_smoke_consumer"
            "-DORANGE_ENGINE_CONSUMER_BUILD_DIR=${CMAKE_BINARY_DIR}/config_smoke_consumer"
            "-DORANGE_ENGINE_CONFIG=$<CONFIG>"
            "-DORANGE_ENGINE_GENERATOR=${CMAKE_GENERATOR}"
            "-DORANGE_ENGINE_PARENT_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
            "-DORANGE_ENGINE_ORANGERENDER_DIR=${OrangeRender_DIR}"
            "-DORANGE_ENGINE_GLM_DIR=${glm_DIR}"
            "-DORANGE_ENGINE_ENTT_DIR=${EnTT_DIR}"
            "-DORANGE_ENGINE_NLOHMANN_JSON_DIR=${nlohmann_json_DIR}"
            "-DORANGE_ENGINE_GLFW3_DIR=${glfw3_DIR}"
            "-DORANGE_ENGINE_BOX2D_DIR=${box2d_DIR}"
            "-DORANGE_ENGINE_VULKAN_INCLUDE_DIR=${Vulkan_INCLUDE_DIR}"
            "-DORANGE_ENGINE_VULKAN_LIBRARY=${Vulkan_LIBRARY}"
            "-DORANGE_ENGINE_VOLK_DIR=${volk_DIR}"
            "-DORANGE_ENGINE_VMA_DIR=${VulkanMemoryAllocator_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install/config_smoke.cmake"
    )

    # editor_build_smoke —— Phase 6 / Task 06-01：证明 tools/OrangeEditor
    # 这个独立 CMake 工程能仅靠 find_package(OrangeEngine) 从零搭起来。
    # 与 config_smoke 同模式（install + configure + build），仅 src 路
    # 径换成 tools/OrangeEditor。
    add_test(
        NAME editor_build_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DORANGE_ENGINE_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DORANGE_ENGINE_INSTALL_DIR=${CMAKE_BINARY_DIR}/editor_build_smoke_install"
            "-DORANGE_ENGINE_CONSUMER_SRC_DIR=${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
            "-DORANGE_ENGINE_CONSUMER_BUILD_DIR=${CMAKE_BINARY_DIR}/editor_build_smoke_consumer"
            "-DORANGE_ENGINE_CONFIG=$<CONFIG>"
            "-DORANGE_ENGINE_GENERATOR=${CMAKE_GENERATOR}"
            "-DORANGE_ENGINE_PARENT_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
            "-DORANGE_ENGINE_ORANGERENDER_DIR=${OrangeRender_DIR}"
            "-DORANGE_ENGINE_GLM_DIR=${glm_DIR}"
            "-DORANGE_ENGINE_ENTT_DIR=${EnTT_DIR}"
            "-DORANGE_ENGINE_NLOHMANN_JSON_DIR=${nlohmann_json_DIR}"
            "-DORANGE_ENGINE_GLFW3_DIR=${glfw3_DIR}"
            "-DORANGE_ENGINE_BOX2D_DIR=${box2d_DIR}"
            "-DORANGE_ENGINE_VULKAN_INCLUDE_DIR=${Vulkan_INCLUDE_DIR}"
            "-DORANGE_ENGINE_VULKAN_LIBRARY=${Vulkan_LIBRARY}"
            "-DORANGE_ENGINE_VOLK_DIR=${volk_DIR}"
            "-DORANGE_ENGINE_VMA_DIR=${VulkanMemoryAllocator_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install/editor_build_smoke.cmake"
    )
endif ()
