# 05_skeletal_animation sample —— Phase 4 / Task 10。
# 加载 DragonBones .json skeleton（vendor/DragonBones/Cocos2DX_3.x/Demos/
# Resources/mecha_1004d_show），Play "idle" 动画，把每根骨骼的 world
# 位置画成小球——Phase 4 范围 SkeletalAnimator.Pose() 只算到 CPU palette，
# GPU skinning 等 Phase 6。
#
# 这条 sample 需要 src/ 在 include path 上：DragonBonesContext.h 是引擎
# 内部头（放在 src/animation/dragonbones/），sample 通过它构造 runtime。
add_executable(05_skeletal_animation main.cpp)

target_link_libraries(05_skeletal_animation PRIVATE OrangeEngine::orange_engine)
target_include_directories(05_skeletal_animation PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/samples/common"
)

# 把 skeleton fixture 路径以编译期宏注入；运行时按它构造文件路径。
# 不拷贝 .json 到 build 目录——sample 直接从源码树读，节省 install 体积。
target_compile_definitions(05_skeletal_animation PRIVATE
    "ORANGE_ENGINE_SKELETON_DATA_DIR=\"${CMAKE_SOURCE_DIR}/vendor/DragonBones/Cocos2DX_3.x/Demos/Resources/mecha_1004d_show\""
)

orange_engine_set_compiler_options(05_skeletal_animation)
