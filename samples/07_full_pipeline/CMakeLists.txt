# 07_full_pipeline sample —— Phase 3 视觉基线综合演示。
# Plane (textured) + Cube (toon) + Sphere (rim_light)，
# DirectionalLight 投影 + 完整 PostProcessChain (HDR / Bloom / Tonemap / LUT)。
add_executable(07_full_pipeline main.cpp)

target_link_libraries(07_full_pipeline PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(07_full_pipeline)
