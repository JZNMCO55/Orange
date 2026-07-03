#ifndef ORANGE_ENGINE_TWEEN_TWEEN_H
#define ORANGE_ENGINE_TWEEN_TWEEN_H

// ---------------------------------------------------------------------------
// Tween —— 代码驱动的补间（tween / interpolation）运行时。把一个标量从 from
// 到 to 在 duration 秒内按缓动曲线插值推进，每帧 Update(dt) 驱动、回调消费当前
// 值（对标 DOTween / iTween 的补间管理器）。典型用途：程序化动画、UI 过渡、
// 相机 / 数值缓动，与关键帧动画 (clip / InterpMode) 正交——本模块是"代码里临时
// 起一段缓动"，不落磁盘、不进 scene 序列化。
//
// handle-based：Add 返回 TweenHandle，Kill / IsActive / GetValue 按句柄操作，
// 句柄绝不复用（Clear 也不重置发号器）。单例式服务——不可拷贝 / 移动（回调可能
// 捕获外部状态，值语义搬运语义不清；同 EventBus）。
//
// 循环模式 LoopMode：Once（到端点停 + 触发 onComplete）/ Loop（回绕重放）/
// PingPong（往返）。Update 对再入 (re-entrant) 安全——回调里可以 Add / Kill，
// 靠快照 (snapshot) 迭代 + 调回调前拷贝出回调对象。
//
// 线程不安全：为单线程游戏循环设计。公共头只依赖标准库 + Easing.h（纯 scalar，
// 不引 glm）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/tween/Easing.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Orange::Engine::Tween
{

    // 循环模式：Once 到端点停并触发 onComplete；Loop 回绕重放；PingPong 往返。
    enum class LoopMode : std::uint8_t
    {
        Once,
        Loop,
        PingPong,
    };

    // 补间句柄 (tween handle)：Add 返回、Kill / IsActive / GetValue 用。id==0 无效
    // （Add 从 1 开始发号，0 永久保留给无效句柄）。
    struct TweenHandle
    {
        std::uint64_t id{0};

        bool IsValid() const noexcept
        {
            return id != 0;
        }
    };

    // 一个补间的描述：把标量从 from 到 to 在 duration 秒内按 ease 插值。
    struct TweenDesc
    {
        float                      from{0.0f};
        float                      to{1.0f};
        float                      duration{1.0f}; // 秒；<=0 视为瞬间完成
        float                      delay{0.0f};    // 开始前延迟秒（这段时间值恒为 from）
        EaseType                   ease{EaseType::Linear};
        LoopMode                   loop{LoopMode::Once};
        std::function<void(float)> onUpdate;   // 每次 Update 用当前值调用（可空）
        std::function<void()>      onComplete; // Once 完成时调用一次（可空；Loop/PingPong 不触发）
    };

    // 补间管理器：批量持有 + 每帧 Update(dt) 推进。handle-based。单例式服务。
    class ORANGE_ENGINE_API TweenManager
    {
    public:
        TweenManager() = default;

        // 不可拷贝 / 移动（单例式服务，held by ref/ptr；同 EventBus）：回调可能捕获
        // 外部状态，值语义搬运会让"哪个 manager 拥有该补间"语义模糊。
        TweenManager(const TweenManager&)            = delete;
        TweenManager& operator=(const TweenManager&) = delete;
        TweenManager(TweenManager&&)                 = delete;
        TweenManager& operator=(TweenManager&&)      = delete;

        // 加一个补间，返回句柄。
        TweenHandle Add(const TweenDesc& desc);

        // 推进所有补间，触发回调，移除完成的 Once。
        void Update(float dt);

        // 移除；无效 / 已移除句柄 → no-op。
        void Kill(TweenHandle handle);

        // 句柄是否仍活跃。
        bool IsActive(TweenHandle handle) const;

        // 当前值；无效句柄返回 0。
        float GetValue(TweenHandle handle) const;

        // 当前活跃补间数。
        std::size_t ActiveCount() const;

        // 清空所有补间（mNextId 不重置，绝不复用句柄）。
        void Clear();

    private:
        struct State
        {
            TweenDesc desc;
            float     elapsed{0.0f};
        };

        std::unordered_map<std::uint64_t, State> mTweens;
        std::uint64_t                            mNextId{1}; // 0 保留给无效句柄
    };

} // namespace Orange::Engine::Tween

#endif // ORANGE_ENGINE_TWEEN_TWEEN_H
