#ifndef ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_HEADLESS_FACTORY_H
#define ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_HEADLESS_FACTORY_H

// HeadlessFactory —— BaseFactory 的"无显示"具体实现。
//
// BaseFactory 把 _buildArmature / _buildSlot / _buildTextureAtlasData 列
// 为纯虚——上游约定每个对接引擎实现自己的 display 路径。本类把它们
// 接到 HeadlessTypes 里的 no-op 显示具体类，让 OrangeEngine 在没有渲
// 染端介入的情况下就能完成 .json/.dbbin → DragonBonesData → Armature
// 的全部 runtime 路径，CPU pose 输出即可工作。
//
// 当前实例：每个 DragonBonesContext 持有一份 HeadlessFactory 单例；
// SkeletonLoader 通过 context 拿到 factory 来 parseDragonBonesData，
// SkeletalAnimator 通过 context.BuildArmature 拿一个 Armature 实例。

#include "HeadlessTypes.h"

namespace Orange::Engine::Animation::DragonBonesBackend
{

class HeadlessFactory final : public dragonBones::BaseFactory
{
public:
    // BaseFactory 默认 ctor 用 _jsonParser；本类显式让 BinaryDataParser
    // 也可用：parseDragonBonesData 默认走 JSONDataParser，调用方需要
    // 二进制 .dbbin 路径时通过 dataParser 参数传 BinaryDataParser。
    explicit HeadlessFactory(dragonBones::DragonBones* runtime);
    ~HeadlessFactory() override = default;

    HeadlessFactory(const HeadlessFactory&)            = delete;
    HeadlessFactory& operator=(const HeadlessFactory&) = delete;

protected:
    // _buildTextureAtlasData：本期不调（暂不做贴图）；保留实现
    // 以满足纯虚要求 + 防御未来误调。textureAtlasData != nullptr 时按
    // upstream 例约定"已有 atlas 数据，本函数挂 GPU 贴图"——本侧无
    // GPU 路径，原值返回不动；nullptr 时返回 borrowObject<HeadlessTextureAtlasData>。
    dragonBones::TextureAtlasData* _buildTextureAtlasData(
        dragonBones::TextureAtlasData* textureAtlasData,
        void*                          textureAtlas) const override;

    // _buildArmature：从对象池借 Armature + 创建 HeadlessArmatureProxy，
    // armature->init 把 proxy 接上。返回的 Armature 由调用方（buildArmature）
    // 推进生命周期；HeadlessArmatureProxy 由 Armature 持有，dispose 时一
    // 起释放（dbClear → 我们自己 delete proxy）——见 cpp 实现。
    dragonBones::Armature* _buildArmature(
        const dragonBones::BuildArmaturePackage& dataPackage) const override;

    // _buildSlot：从对象池借 HeadlessSlot；rawDisplay / meshDisplay 都传
    // nullptr（Slot::init 接受 void* nullable）。HeadlessSlot 的 _initDisplay
    // 全部 no-op，所以传 nullptr 完全合法。
    dragonBones::Slot* _buildSlot(
        const dragonBones::BuildArmaturePackage& dataPackage,
        const dragonBones::SlotData*             slotData,
        dragonBones::Armature*                   armature) const override;
};

}  // namespace Orange::Engine::Animation::DragonBonesBackend

#endif  // ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_HEADLESS_FACTORY_H
