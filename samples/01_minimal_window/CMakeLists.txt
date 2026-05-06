# 最小窗口 sample —— 构造 AppConfig、AppHost、Run，验证 Phase 1 主
# 循环骨架完整可跑。可执行文件命名直接用目录名，所有 sample 输出统
# 一落到 build/bin/<config>/。
add_executable(01_minimal_window main.cpp)

target_link_libraries(01_minimal_window PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(01_minimal_window)
