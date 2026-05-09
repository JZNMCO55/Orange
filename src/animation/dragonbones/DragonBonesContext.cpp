#include "DragonBonesContext.h"

#include "HeadlessFactory.h"

// 引入 runtime 完整头——本 cpp 是 OrangeEngine 内**唯一**直接 include
// <dragonBones/...> 的地方（Phase 4 / Task 02），CLAUDE.md "Header
// isolation" 要求 dragonBones 头只在 src/animation/dragonbones/** 下出现。
//
// orange_engine 自身按 /W4 /WX 编译，但 DragonBones runtime + 其内嵌的
// rapidjson 头有大量 C4244 / C5054 / C26451 等"窄化转换 / 跨枚举位运算"
// 警告——这些是上游代码的事，本侧不修。用 MSVC pragma 在 include 段
// 局部压成 /W0 即可：本 .cpp 自己写的代码段（pragma pop 之后）仍按
// 项目级 /W4 /WX 受检。
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#include <dragonBones/DragonBonesHeaders.h>
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <functional>
#include <string>

namespace Orange::Engine::Animation::DragonBonesBackend
{

class NoopEventDispatcher final : public dragonBones::IEventDispatcher
{
public:
    NoopEventDispatcher()           = default;
    ~NoopEventDispatcher() override = default;

    bool hasDBEventListener(const std::string& /*type*/) const override
    {
        return false;
    }

    void dispatchDBEvent(const std::string& /*type*/, dragonBones::EventObject* /*value*/) override
    {
        // no-op：runtime 想派发的事件在 Phase 4 / Task 02 阶段全部丢弃。
        // Audio（Task 09）上线后接 Sound 事件；游戏侧自行处理 frame
        // event 时由 SkeletalAnimator（Task 03）改用真 dispatcher。
    }

    void addDBEventListener(const std::string& /*type*/,
                            const std::function<void(dragonBones::EventObject*)>& /*listener*/) override
    {
    }

    void removeDBEventListener(const std::string& /*type*/,
                               const std::function<void(dragonBones::EventObject*)>& /*listener*/) override
    {
    }
};

DragonBonesContext::DragonBonesContext()
    : mpEventDispatcher(std::make_unique<NoopEventDispatcher>())
    , mpRuntime(std::make_unique<dragonBones::DragonBones>(mpEventDispatcher.get()))
    , mpFactory(std::make_unique<HeadlessFactory>(mpRuntime.get()))
{
}

DragonBonesContext::~DragonBonesContext()
{
    // 销毁顺序：factory 先（它持有的 DragonBonesData 缓存要在 runtime 仍
    // 活时清空，因为 dispose 路径会回收对象进 BaseObject 池——池就是
    // runtime 内部）。然后 runtime，最后 dispatcher。unique_ptr 反向声明
    // 顺序保证这一点；显式 reset 让顺序在意图层面也清楚。
    mpFactory.reset();
    mpRuntime.reset();
    mpEventDispatcher.reset();
}

void DragonBonesContext::AdvanceTime(float dt)
{
    if (!mpRuntime || dt < 0.0f)
    {
        return;
    }
    mpRuntime->advanceTime(dt);
}

dragonBones::WorldClock* DragonBonesContext::Clock() const noexcept
{
    return mpRuntime ? mpRuntime->getClock() : nullptr;
}

dragonBones::IEventDispatcher* DragonBonesContext::EventDispatcher() const noexcept
{
    return mpEventDispatcher.get();
}

dragonBones::DragonBonesData* DragonBonesContext::ParseDragonBonesData(
    const char*        bytes,
    std::size_t        byteCount,
    const std::string& cacheName,
    bool               binary)
{
    if (!mpFactory || bytes == nullptr || byteCount == 0)
    {
        return nullptr;
    }
    if (binary)
    {
        // BinaryDataParser 接口（factory.parseDragonBonesData 不接 binary
        // 参数）——直接走 BaseFactory 的二进制重载是 _binaryParser；
        // upstream 的 cocos backend 走 dataParser 切换。本期只验 JSON
        // 路径（test 用 .json）；binary 路径暂用 setData 走 _binaryParser，
        // 留给后续真消费 .dbbin 时按需补：先解析、再 addDragonBonesData。
        // ——本侧不强求 binary 通过，先保 JSON 路径稳定。
        return nullptr;
    }
    return mpFactory->parseDragonBonesData(bytes, cacheName, /*scale=*/1.0f);
}

dragonBones::Armature* DragonBonesContext::BuildArmature(
    const std::string& armatureName,
    const std::string& dragonBonesName)
{
    if (!mpFactory)
    {
        return nullptr;
    }
    // 先校验：避免触发 BaseFactory 内部 DRAGONBONES_ASSERT(false,
    // "No armature data: ...") 在 debug 构建里直接 abort 进程。upstream
    // 把"找不到 armature"算 fatal client error，但本侧 API 契约把它当
    // 普通失败（返回 nullptr，调用方决定后续）。
    auto* data = mpFactory->getDragonBonesData(dragonBonesName);
    if (data == nullptr || data->getArmature(armatureName) == nullptr)
    {
        return nullptr;
    }
    return mpFactory->buildArmature(armatureName, dragonBonesName);
}

void DragonBonesContext::DestroyArmature(dragonBones::Armature* armature)
{
    if (armature == nullptr)
    {
        return;
    }
    auto* proxy = armature->getProxy();
    // armature->dispose 把 armature 还回对象池；HeadlessArmatureProxy::
    // dispose 内部调 armature->dispose 然后置自身指针为 null——所以
    // 这里调 proxy->dispose 走完整路径，再 delete proxy（proxy 不进池）。
    if (proxy != nullptr)
    {
        proxy->dispose(/*disposeProxy=*/true);
        delete proxy;
    }
    else
    {
        armature->dispose();
    }
}

}  // namespace Orange::Engine::Animation::DragonBonesBackend
