#ifndef ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_DRAGON_BONES_CONTEXT_H
#define ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_DRAGON_BONES_CONTEXT_H

// DragonBonesContext —— DragonBones C++ runtime 的"使用入口"。
//
// runtime（vendor/DragonBones/...）入口对象 dragonBones::DragonBones 必须
// 拿一个 IEventDispatcher* 构造；那个 dispatcher 是 runtime 把 anim 事件
// （loop / complete / frame event / sound 触发）回调出来的接缝。
// 当前只接通 runtime，不接通 Audio / 不接通游戏侧 UI——这里给一
// 个 no-op dispatcher 顶上：实现 4 个虚函数全部空体，让 runtime 构造合
// 法、advanceTime 真跑，但所有事件都被默默吞掉。Audio 上线后
// 由它处理 Sound 事件、game 层处理 frame event。
//
// 头隔离：本头**不**暴露任何 dragonBones 类型 / 头到 include/orange/
// engine/。dragonBones 类型只前向声明，真实头只在 DragonBonesContext.cpp
// 里 include。这保证 CLAUDE.md "Header isolation" 不变量
//   `<dragonBones/...>` 仅出现在 src/animation/dragonbones/**
// 在本文件层面已成立——而本头作为 src 内部接口，可被 SkeletalAnimator
// 实现 / smoke 测试 共同消费。

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace dragonBones
{
class Armature;
class DragonBones;
class DragonBonesData;
class IEventDispatcher;
class WorldClock;
}  // namespace dragonBones

namespace Orange::Engine::Animation::DragonBonesBackend
{

// runtime 持有的 no-op 事件派发器：4 个 IEventDispatcher 虚函数全部空体。
// 暴露为不完整类型——本头声明、cpp 定义——让外面看到指针就够。
class NoopEventDispatcher;

// runtime 用的"无显示"BaseFactory；ParseDragonBonesData / BuildArmature 走
// 它分发。声明在本头但定义在 src/animation/dragonbones/HeadlessFactory.h——
// 让 DragonBonesContext.cpp 是唯一直接 include HeadlessFactory.h 的位置，
// 其他 src/animation/dragonbones/ 内部消费方走 context.BuildArmature 公共面。
class HeadlessFactory;

// runtime 入口对象的 RAII 包装。
//   * ctor 创建 NoopEventDispatcher + dragonBones::DragonBones；
//   * AdvanceTime(dt) 推进内部 WorldClock；
//   * dtor 析构 dragonBones 对象（同时释放它内部缓存的 EventObject 池）
//     再析构 dispatcher——顺序由 unique_ptr 反向声明保证。
//
// 单实例 = 单 runtime "world"——SkeletalAnimator 多实例会共享同一个
// DragonBonesContext（通过 AnimatorRegistry 注入），这与 DragonBones 单
// IEventDispatcher / 单 WorldClock 的设计一致。
class DragonBonesContext
{
public:
    DragonBonesContext();
    ~DragonBonesContext();

    DragonBonesContext(const DragonBonesContext&)            = delete;
    DragonBonesContext& operator=(const DragonBonesContext&) = delete;

    // 把 runtime 时间向前推 dt 秒。dt < 0 → no-op。
    void AdvanceTime(float dt);

    // runtime 内部 WorldClock 入口（dragonBones armature 注册到它身上）。
    // dtor 之后返回值无效，调用方自己保证生命周期。
    dragonBones::WorldClock* Clock() const noexcept;

    // 本 context 持有的 no-op dispatcher。SkeletalAnimator 创建 armature
    // 时把它传进 BaseFactory；测试也可以用它判定 ctor 链条是否成立。
    dragonBones::IEventDispatcher* EventDispatcher() const noexcept;

    // 解析 .json / .dbbin 字节为 DragonBonesData 并以 cacheName 缓存到内
    // 部 factory；后续 BuildArmature 用 cacheName 索引。binary == false →
    // 走 JSONDataParser；binary == true → 走 BinaryDataParser。
    // 返回值：解析失败 → nullptr；同 cacheName 重复解析 → 旧数据（runtime
    // 内置 dedup）。bytes 缓冲在解析期间必须可读（runtime 不持有，立即拷贝）。
    dragonBones::DragonBonesData* ParseDragonBonesData(const char*        bytes,
                                                       std::size_t        byteCount,
                                                       const std::string& cacheName,
                                                       bool               binary);

    // 用已注册的 dragonBonesName 创建一个 armature 实例。armatureName 来自
    // .json 内 armature[].name；不存在 → 返回 nullptr。armature 与挂在它身
    // 上的 HeadlessArmatureProxy 由 DestroyArmature 负责释放。
    dragonBones::Armature* BuildArmature(const std::string& armatureName,
                                         const std::string& dragonBonesName);

    // armature 的对应 RAII 释放：dispose 内部走 proxy->dispose 把 armature
    // 还回 BaseObject 池，再 delete proxy（HeadlessArmatureProxy 不是池对象）。
    // armature == nullptr → no-op。
    void DestroyArmature(dragonBones::Armature* armature);

private:
    // 声明顺序 = 销毁顺序（反向）。dragonBones::DragonBones 持 dispatcher
    // 指针——必须先析构 runtime 再析构 dispatcher；factory 持 DragonBones*，
    // 必须比 runtime 先析构。
    std::unique_ptr<NoopEventDispatcher>      mpEventDispatcher;
    std::unique_ptr<dragonBones::DragonBones> mpRuntime;
    std::unique_ptr<HeadlessFactory>          mpFactory;
};

}  // namespace Orange::Engine::Animation::DragonBonesBackend

#endif  // ORANGE_ENGINE_SRC_ANIMATION_DRAGONBONES_DRAGON_BONES_CONTEXT_H
