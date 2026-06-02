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
orange_engine_add_test(scene_world_test         scene/SceneWorldTest.cpp)
orange_engine_add_test(scene_world_partition_test scene/WorldPartitionTest.cpp)
orange_engine_add_test(scene_subtree_clone_test scene/SceneSubtreeCloneTest.cpp)
orange_engine_add_test(scene_serialization_test scene/SceneSerializationTest.cpp)
orange_engine_add_test(scene_entity_guid_test   scene/EntityGuidTest.cpp)
orange_engine_add_test(scene_prefab_test        scene/PrefabTest.cpp)
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
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c")
target_link_libraries(headless_mesh_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(headless_mesh_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf")
if (EXISTS "${_orange_mikkt_fixture}")
    target_compile_definitions(headless_mesh_import_test PRIVATE
        "ORANGE_ENGINE_GLTF_FIXTURE=\"${_orange_mikkt_fixture}\"")
endif ()
orange_engine_set_compiler_options(headless_mesh_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
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
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c")
target_link_libraries(gltf_embedded_texture_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(gltf_embedded_texture_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf")
orange_engine_set_compiler_options(gltf_embedded_texture_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
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
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MetaSidecar.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import/MeshTangentGen.cpp"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/MaterialFileIO.cpp"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c")
target_link_libraries(gltf_scene_import_test PRIVATE OrangeEngine::orange_engine)
target_include_directories(gltf_scene_import_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor"
    "${CMAKE_SOURCE_DIR}/tools/OrangeEditor/import"
    "${CMAKE_SOURCE_DIR}/vendor/tinyobjloader"
    "${CMAKE_SOURCE_DIR}/vendor/mikktspace"
    "${CMAKE_SOURCE_DIR}/vendor/cgltf")
orange_engine_set_compiler_options(gltf_scene_import_test)
if (MSVC)
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "/W0")
else ()
    set_source_files_properties(
        "${CMAKE_SOURCE_DIR}/vendor/mikktspace/mikktspace.c"
        PROPERTIES COMPILE_FLAGS "-w")
endif ()
add_test(NAME gltf_scene_import_test COMMAND gltf_scene_import_test)

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
orange_engine_add_test(light_and_shadow_test    render/LightAndShadowTest.cpp)
orange_engine_add_test(material_schema_test     render/MaterialSchemaTest.cpp)
orange_engine_add_test(pipeline_template_cache_test render/PipelineTemplateCacheTest.cpp)
orange_engine_add_test(pipeline_hdr_target_test    render/PipelineHdrTargetTest.cpp)
orange_engine_add_test(pipeline_offscreen_test     render/PipelineOffscreenTest.cpp)
orange_engine_add_test(pipeline_halo_conditional_test render/PipelineHaloConditionalTest.cpp)
orange_engine_add_test(pipeline_render_to_texture_test render/PipelineRenderToTextureTest.cpp)
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

# Input：ActionMap 状态机 + JSON 加载 + InputContext 栈语义（Phase 4 / Task 08）
orange_engine_add_test(action_map_test         input/ActionMapTest.cpp)
orange_engine_add_test(input_context_stack_test input/InputContextStackTest.cpp)
target_compile_definitions(action_map_test PRIVATE
    "ORANGE_ENGINE_INPUT_TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/assets/configs\""
)

# Audio：AudioEngine init/shutdown smoke + SoundInstance 边界（Phase 4 / Task 09）
orange_engine_add_test(audio_engine_smoke_test  audio/AudioEngineSmokeTest.cpp)

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
