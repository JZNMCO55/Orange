# 14_pbr_ibl sample —— PBR + IBL 完整三件套 demo
# 3 metallic × 3 roughness 球阵 + 1 DirectionalLight + EnvironmentComponent
# 挂 HDR equirect（assets/environments/default_outdoor.hdr）。启动期调
# Pipeline::BakeIblFromWorld 走完 IblBaker 三件套，PBR shader IBL 段从
# dummy 切到真实烘焙产物——金属球反射环境、阴影区有环境光填充。
#
# 命令行 `--furnace` 切换到程序化 1×1 全白 RGBA32Float TextureAsset 作为
# cubemap，white furnace test 验能量守恒（任意材质应输出近似白色）。
add_executable(14_pbr_ibl main.cpp)

target_link_libraries(14_pbr_ibl PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(14_pbr_ibl)
