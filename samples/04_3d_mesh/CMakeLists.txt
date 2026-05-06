# 04_3d_mesh sample —— Phase 2 收尾的 3D 视觉验证：透视相机 + 24 顶点
# 立方体 + 每帧 TransformComponent 旋转。Pipeline 复用 03_textured_quad
# 的 textured_mesh shader（每面独立 UV [0,1]² → fragment shader 程序式
# 棋盘 per face）。
add_executable(04_3d_mesh main.cpp)

target_link_libraries(04_3d_mesh PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(04_3d_mesh)
