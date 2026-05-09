# 07_full_pipeline sample —— Phase 4 / Task 11 收官里程碑。
# DragonBones mecha 角色（idle/walk/jump 切换）+ Box2D 物理 body +
# InputContext 键盘控制 + AudioEngine 跳跃音效（程序生成 beep）+ 完整
# Phase 3 视觉基线（DirectionalLight / shadow / bloom / tonemap）。
#
# Animation backend 走 src/animation/dragonbones/DragonBonesContext.h
# （引擎内部头），sample target 通过 target_include_directories 把
# src/ 加进来。CLAUDE.md 头隔离不变量仍成立——sample 自己不直接
# include <dragonBones/...>。
add_executable(07_full_pipeline main.cpp)

target_link_libraries(07_full_pipeline PRIVATE OrangeEngine::orange_engine)
target_include_directories(07_full_pipeline PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/samples/common"
)

# 资源路径 / actions JSON 路径 编译期宏注入；sample 启动直接按它们读。
target_compile_definitions(07_full_pipeline PRIVATE
    "ORANGE_ENGINE_SKELETON_DATA_DIR=\"${CMAKE_SOURCE_DIR}/vendor/DragonBones/Cocos2DX_3.x/Demos/Resources/mecha_1406\""
    "ORANGE_ENGINE_ACTIONS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/configs\""
)

orange_engine_set_compiler_options(07_full_pipeline)
