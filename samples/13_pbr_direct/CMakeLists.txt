# 13_pbr_direct sample —— PBR direct lighting 3×3 球阵
# (3 metallic × 3 roughness) + 1 DirectionalLight；IBL 槽位走 Pipeline
# dummy 1×1 黑纹理 → direct-only 退化。validate Cook-Torrance + Schlick
# + GGX + Smith correlated + Lambert diffuse。
add_executable(13_pbr_direct main.cpp)

target_link_libraries(13_pbr_direct PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(13_pbr_direct)
