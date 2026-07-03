#ifndef ORANGE_ENGINE_ASSET_SKELETON_ASSET_H
#define ORANGE_ENGINE_ASSET_SKELETON_ASSET_H

// ---------------------------------------------------------------------------
// SkeletonAsset —— DragonBones .json / .dbbin 解析后的"骨架元数据 + 工厂
// 索引"对。
//
// 一个 skeleton 文件可能含多个 armature（DragonBones 把多套骨架打包到
// 一个 DragonBonesData 里）。本 asset 持有：
//   * dragonBonesName：把 file → factory 内部缓存键串起来的身份名；
//   * armatures[]：每个 armature 的可消费元数据（名字 / bone names /
//     animation names），调用方在不构造 armature 实例的前提下也能查询；
//   * 不直接暴露任何 dragonBones runtime 类型——SkeletalAnimator 通过
//     dragonBonesName + armatureName 找回 factory 里的 data，再 build。
//
// 头隔离：本头不 include `<dragonBones/...>`，CLAUDE.md 不变量成立。dragonBones
// 类型只在 src/animation/dragonbones/SkeletonAsset.cpp（PIMPL 实现侧）出现。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Asset
{

    // 单个 armature 的元数据快照——loader 解析时一次性收集；运行期不再变更。
    struct ORANGE_ENGINE_API ArmatureMeta
    {
        std::string              name;           // armature 名字（factory.buildArmature 用）
        std::vector<std::string> boneNames;      // index 与 runtime bone vector 顺序对齐
        std::vector<std::string> animationNames; // 可播放动画名清单
    };

    class ORANGE_ENGINE_API SkeletonAsset
    {
    public:
        SkeletonAsset(); // 空 skeleton，仅供占位 / 错误路径返回
        SkeletonAsset(std::string dragonBonesName, std::vector<ArmatureMeta> armatures);
        ~SkeletonAsset();

        SkeletonAsset(const SkeletonAsset&)            = delete;
        SkeletonAsset& operator=(const SkeletonAsset&) = delete;

        SkeletonAsset(SkeletonAsset&&) noexcept;
        SkeletonAsset& operator=(SkeletonAsset&&) noexcept;

        // factory 内 DragonBonesData 缓存键。SkeletalAnimator 用它 +
        // armatureName 调 context.BuildArmature 查回真 armature data。
        std::string_view DragonBonesName() const noexcept;

        // 本 skeleton 文件含的所有 armature 元数据。索引顺序 = .json 内
        // armature[] 数组顺序（runtime parse 维持稳定）。
        std::span<const ArmatureMeta> Armatures() const noexcept;

        // 按名查找 armature 元数据；找不到 → nullptr。便利 helper，调用方
        // 也可以自己遍历 Armatures()。
        const ArmatureMeta* FindArmature(std::string_view name) const noexcept;

        // 骨架是否承载有效内容。loader 失败时返回的 stub asset 在此为 false。
        bool Empty() const noexcept;

    private:
        std::string               mDragonBonesName;
        std::vector<ArmatureMeta> mArmatures;
    };

} // namespace Orange::Engine::Asset

#endif // ORANGE_ENGINE_ASSET_SKELETON_ASSET_H
