#include "HeadlessFactory.h"

namespace Orange::Engine::Animation::DragonBonesBackend
{

    HeadlessFactory::HeadlessFactory(dragonBones::DragonBones* runtime)
        : dragonBones::BaseFactory(/*dataParser=*/nullptr)
    {
        // BaseFactory 内部成员 _dragonBones 的赋值由上游"factory 子类自己
        // 在 ctor 里塞"约定（CCFactory 也这样做）。
        this->_dragonBones = runtime;
        // autoSearch=true：buildArmature 在按名 lookup 失败后会扫所有已注册
        // DragonBonesData，按 armatureName 匹配；让"loader 用 path 缓存 +
        // animator 用同一 path 索引"的契约对名字差异（路径分隔符 / 大小
        // 写）更鲁棒。
        this->autoSearch = true;
    }

    dragonBones::TextureAtlasData* HeadlessFactory::_buildTextureAtlasData(
        dragonBones::TextureAtlasData* textureAtlasData,
        void* /*textureAtlas*/) const
    {
        if (textureAtlasData != nullptr)
        {
            // 已存在 atlas 数据——本期不挂 GPU 贴图，原对象返回。
            return textureAtlasData;
        }
        // upstream 约定 nullptr 时新建一个，BaseObject 对象池借出。
        return dragonBones::BaseObject::borrowObject<HeadlessTextureAtlasData>();
    }

    dragonBones::Armature* HeadlessFactory::_buildArmature(
        const dragonBones::BuildArmaturePackage& dataPackage) const
    {
        auto* armature = dragonBones::BaseObject::borrowObject<dragonBones::Armature>();

        // proxy 由 armature 间接持有（armature->_proxy = proxy）；不进对象
        // 池。armature dispose 时调 _proxy->dispose；HeadlessArmatureProxy::
        // dispose 把 armature 还池但**不**自删——所以由 SkeletalAnimator /
        // SkeletonLoader 等调用方持 unique_ptr<HeadlessArmatureProxy> 与
        // armature 的生命周期同期管理（见 SkeletalAnimator 实现）。
        auto* proxy = new HeadlessArmatureProxy();

        // armature->init 内部会调 proxy->dbInit(this)，让 proxy 记下 armature
        // 指针。display 参数（void*）传 nullptr——本侧无任何 display 节点。
        armature->init(dataPackage.armature, proxy, /*display=*/nullptr, _dragonBones);

        return armature;
    }

    dragonBones::Slot* HeadlessFactory::_buildSlot(
        const dragonBones::BuildArmaturePackage& /*dataPackage*/,
        const dragonBones::SlotData* slotData,
        dragonBones::Armature*       armature) const
    {
        auto* slot = dragonBones::BaseObject::borrowObject<HeadlessSlot>();
        slot->init(slotData, armature, /*rawDisplay=*/nullptr, /*meshDisplay=*/nullptr);
        return slot;
    }

} // namespace Orange::Engine::Animation::DragonBonesBackend
