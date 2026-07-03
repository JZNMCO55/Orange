#ifndef ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_HEADLESS_TYPES_H
#define ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_HEADLESS_TYPES_H

// HeadlessTypes —— DragonBones runtime 的"无显示后端"具体类。
//
// DragonBones 的 BaseFactory / Slot / TextureAtlasData / IArmatureProxy
// 在上游全是抽象接口（专为对接 Cocos2D-x / SFML / Egret 等渲染引擎而
// 设计——它们各自实现一套 display 路径）。OrangeEngine 当前
// 只需要 CPU 端 pose（per-bone matrix palette）输出，**不**
// 进任何渲染——所以这里给 runtime 配上一组"显示路径全部 no-op"的具
// 体类，让 Armature 能被 init / advanceTime / 析构、bone 全局变换正确
// 计算，但任何与 GPU / display node 相关的虚函数全部空体。
//
// 命名空间：dragonBones（与 runtime 保持同一 namespace，让 BaseFactory
// 子类实现起来无需到处 dragonBones:: 前缀）；这一点是 runtime 友好
// 写法的一部分，与 OrangeEngine 自身的 `Orange::Engine::*` 命名规范
// 不冲突——这些类型不出现在公共 API 表面。
//
// 头隔离：本文件 include <dragonBones/...>，按 CLAUDE.md 不变量只允许
// 出现在 src/animation/dragonbones/**——本头位置满足。

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <dragonBones/DragonBonesHeaders.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Orange::Engine::Animation::DragonBonesBackend
{

    // HeadlessSlot —— Slot 的"display 全部 no-op"版本。
    // Slot 在 DragonBones 模型里挂 image / mesh / armature display；headless
    // 后端不挂任何 display，所有 display 相关虚函数空体即可。bone 全局变换
    // 不在 Slot 路径上——它在 Bone::update()，与本类无关。
    class HeadlessSlot final : public dragonBones::Slot
    {
        BIND_CLASS_TYPE_A(HeadlessSlot);

    protected:
        void _initDisplay(void* /*value*/, bool /*isRetain*/) override {}
        void _disposeDisplay(void* /*value*/, bool /*isRelease*/) override {}
        void _onUpdateDisplay() override {}
        void _addDisplay() override {}
        void _replaceDisplay(void* /*value*/, bool /*isArmatureDisplay*/) override {}
        void _removeDisplay() override {}
        void _updateZOrder() override {}
        void _updateVisible() override {}
        void _updateBlendMode() override {}
        void _updateColor() override {}
        void _updateFrame() override {}
        void _updateMesh() override {}
        void _updateTransform() override {}
        void _identityTransform() override {}

        void _onClear() override
        {
            Slot::_onClear();
        }
    };

    // HeadlessTextureData —— Texture 数据占位。本期不解析贴图（不调
    // parseTextureAtlasData），故本类的 ctor / dtor 不会被运行时触发；仅为
    // 满足 TextureAtlasData::createTexture() 的纯虚返回类型 + TextureData
    // 的纯虚析构存在。BaseObject 要求 getClassTypeIndex/typeIndex，BIND_
    // CLASS_TYPE_A 一并合成。
    class HeadlessTextureData final : public dragonBones::TextureData
    {
        BIND_CLASS_TYPE_A(HeadlessTextureData);

    public:
        void _onClear() override
        {
            TextureData::_onClear();
        }
    };

    // HeadlessTextureAtlasData —— 同上。createTexture() 返回 HeadlessTextureData
    // 实例供 runtime 将来需要时使用，本期路径不会真触达。
    class HeadlessTextureAtlasData final : public dragonBones::TextureAtlasData
    {
        BIND_CLASS_TYPE_A(HeadlessTextureAtlasData);

    public:
        dragonBones::TextureData* createTexture() const override
        {
            return new HeadlessTextureData();
        }

    protected:
        void _onClear() override
        {
            TextureAtlasData::_onClear();
        }
    };

    // HeadlessArmatureProxy —— Armature 必须挂的 IArmatureProxy。本类把
    // dbInit / dbClear / dbUpdate（每帧调用一次的 display update 钩子）和
    // IEventDispatcher 4 个方法全部 no-op；getArmature / getAnimation 返回
    // init 时记下的指针。
    class HeadlessArmatureProxy final : public dragonBones::IArmatureProxy
    {
    public:
        HeadlessArmatureProxy()           = default;
        ~HeadlessArmatureProxy() override = default;

        HeadlessArmatureProxy(const HeadlessArmatureProxy&)            = delete;
        HeadlessArmatureProxy& operator=(const HeadlessArmatureProxy&) = delete;

        // IArmatureProxy
        void dbInit(dragonBones::Armature* armature) override
        {
            mpArmature = armature;
        }
        void dbClear() override
        {
            mpArmature = nullptr;
        }
        void dbUpdate() override {}
        void dispose(bool /*disposeProxy*/) override
        {
            if (mpArmature != nullptr)
            {
                mpArmature->dispose();
                mpArmature = nullptr;
            }
        }
        dragonBones::Armature*  getArmature() const override { return mpArmature; }
        dragonBones::Animation* getAnimation() const override
        {
            return mpArmature != nullptr ? mpArmature->getAnimation() : nullptr;
        }

        // IEventDispatcher：armature 自身 anim 事件（loop / complete / frame）
        // 在 headless 后端按"先丢"处理，与 DragonBonesContext 上的 NoopEvent
        // Dispatcher 对齐。Audio / 游戏侧 frame event 等真正落地后
        // 在外层（SkeletalAnimator）替换为转发实现。
        bool hasDBEventListener(const std::string& /*type*/) const override
        {
            return false;
        }
        void dispatchDBEvent(const std::string& /*type*/, dragonBones::EventObject* /*value*/) override {}
        void addDBEventListener(const std::string& /*type*/,
                                const std::function<void(dragonBones::EventObject*)>& /*listener*/) override
        {
        }
        void removeDBEventListener(const std::string& /*type*/,
                                   const std::function<void(dragonBones::EventObject*)>& /*listener*/) override
        {
        }

    private:
        dragonBones::Armature* mpArmature{nullptr};
    };

} // namespace Orange::Engine::Animation::DragonBonesBackend

#endif // ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_HEADLESS_TYPES_H
