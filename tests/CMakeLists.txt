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
orange_engine_add_test(scene_world_test         scene/SceneWorldTest.cpp)
orange_engine_add_test(render_interface_test    render/RenderInterfaceTest.cpp)
orange_engine_add_test(render_scene_test        render/RenderSceneTest.cpp)
orange_engine_add_test(material_interface_test  render/MaterialInterfaceTest.cpp)
orange_engine_add_test(builtin_materials_test   render/BuiltinMaterialsTest.cpp)

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
            "-DORANGE_ENGINE_VULKAN_INCLUDE_DIR=${Vulkan_INCLUDE_DIR}"
            "-DORANGE_ENGINE_VULKAN_LIBRARY=${Vulkan_LIBRARY}"
            "-DORANGE_ENGINE_VOLK_DIR=${volk_DIR}"
            "-DORANGE_ENGINE_VMA_DIR=${VulkanMemoryAllocator_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/install/config_smoke.cmake"
    )
endif ()
