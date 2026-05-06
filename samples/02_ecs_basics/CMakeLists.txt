# ECS 基础 sample —— 控制台程序，纯 ECS 演示，不开窗口、不接 Pipeline。
# 让"实体-组件-查询"这套心智模型独立呈现。
add_executable(02_ecs_basics main.cpp)

target_link_libraries(02_ecs_basics PRIVATE OrangeEngine::orange_engine)

orange_engine_set_compiler_options(02_ecs_basics)
