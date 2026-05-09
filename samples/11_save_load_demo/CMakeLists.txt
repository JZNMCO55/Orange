# 11_save_load_demo sample —— Phase 5.5 收尾。把 SaveGameRegistry /
# SaveGameSystem / SlotManager / SavePath / AutosaveScheduler 全部串成
# 一个完整的"玩家进度存取"闭环。
#
# 关卡内容：
#   * player：toon 方块，WASD 平移（无物理）
#   * 3 个 token：emissive 小方块，触碰收集
#   * ground：textured 地砖
#   * sun：DirectionalLight
#
# 操作：
#   WASD / 方向键 移动；F5 = 保存 slot1；F9 = 加载 slot1；F1 = 列出 slot
#
# 文件位置：
#   * scene: assets/scenes/save_load_demo.scene.json
#   * actions: assets/configs/save_load_demo.actions.json
#   * 存档：%APPDATA%/OrangeEngine/SaveLoadDemo/saves/slot1.save 等

add_executable(11_save_load_demo main.cpp)
target_link_libraries(11_save_load_demo PRIVATE OrangeEngine::orange_engine)
orange_engine_set_compiler_options(11_save_load_demo)

set_target_properties(11_save_load_demo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:11_save_load_demo>"
)

target_compile_definitions(11_save_load_demo PRIVATE
    "ORANGE_SAMPLE_11_SCENE_PATH=\"${CMAKE_SOURCE_DIR}/assets/scenes/save_load_demo.scene.json\""
    "ORANGE_SAMPLE_11_ACTIONS_PATH=\"${CMAKE_SOURCE_DIR}/assets/configs/save_load_demo.actions.json\""
)
