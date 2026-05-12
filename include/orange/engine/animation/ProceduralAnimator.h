#ifndef ORANGE_ENGINE_ANIMATION_PROCEDURAL_ANIMATOR_H
#define ORANGE_ENGINE_ANIMATION_PROCEDURAL_ANIMATOR_H

// ---------------------------------------------------------------------------
// ProceduralAnimator —— IAnimator 的 shader-uniform 驱动后端。
//
// 与 SkeletalAnimator 配对、并列存在，承担引擎承诺的"双后端"中
// 第二条路径：不走 skeleton bone hierarchy，直接把"时间 → uniform 值"
// 的曲线 / 噪声 / 程序式函数挂到一个 MaterialInstance 上——典型用
// 例：史莱姆 noise 振幅、dissolve 进度、UV 流动等不依赖骨架但需要
// 时间驱动的 shader 效果。
//
// 用法：
//     auto* mi = system.GetInstance(...);
//     ProceduralAnimator anim(mi);
//     anim.AddChannel<float>("dissolve_t",
//                            [](float t) { return t / 2.0f; });
//     anim.AddChannel<float>("noise_amp",
//                            [](float t) { return 0.5f + 0.5f * std::sin(t * 6.28f); });
//     // 主循环：
//     anim.Tick(dt);  // 每帧把当前 elapsed 喂进所有 channel.fn，调
//                     // mi->SetUniform 写覆盖表
//
// **Material UBO 接通**：本期 SetUniform 写到 MaterialInstance 内部 override
// 表；Pipeline 在 Material UBO 落地前**不**自动把这张表 push
// 到 GPU——所以 sample 里看不到效果。等 UBO 路径上线，本类不需重做：
// override 表已写对，到时只是 Pipeline 路径多一个"读表 → push-constant"
// 的步骤。
//
// 模板的 T 受限于 MaterialInstance::SetUniform 的重载集合：
// {float, std::int32_t, glm::vec2, glm::vec3, glm::vec4, glm::mat4}。
// 其它类型在实例化时由 SFINAE 报错。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/render/MaterialInstance.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Orange::Engine::Animation
{

class ORANGE_ENGINE_API ProceduralAnimator final : public IAnimator
{
public:
    // target 可为 nullptr——半构造态，Tick 仍合法（不写 mi），调用方
    // 之后 SetTarget 把 target 接上即可继续工作；elapsed 不重置。
    explicit ProceduralAnimator(Render::MaterialInstance* target = nullptr) noexcept;
    ~ProceduralAnimator() override;

    ProceduralAnimator(const ProceduralAnimator&)            = delete;
    ProceduralAnimator& operator=(const ProceduralAnimator&) = delete;
    ProceduralAnimator(ProceduralAnimator&&)                 = delete;
    ProceduralAnimator& operator=(ProceduralAnimator&&)      = delete;

    // 切换驱动目标。target == nullptr → 暂停写入（Tick 仍推 elapsed）。
    void                       SetTarget(Render::MaterialInstance* target) noexcept;
    Render::MaterialInstance*  GetTarget() const noexcept;

    // 注册一条 channel：每 Tick 用 elapsed 调 fn 算出当前值，写到 target
    // 的 SetUniform(name, value)。T ∈ {float, int32_t, vec2, vec3, vec4, mat4}；
    // 其它 T 在实例化时直接失败（MaterialInstance 没对应 SetUniform 重载）。
    //
    // 同名 channel 重复 Add 不会去重——按"后写覆盖前写"的天然语义即可
    // （MaterialInstance::SetUniform 自身就这么定）。如果调用方想替换 fn，
    // 调 ClearChannels 再重 Add。
    template <typename T>
    void AddChannel(std::string_view name, std::function<T(float)> fn);

    // 清空所有 channel。elapsed 不重置——这是"换皮"路径（同一 procedural
    // 时序、不同 channel 集），与 SkeletalAnimator::Play 的 fade 心智不同。
    void ClearChannels() noexcept;

    // IAnimator
    // 推进 elapsed += max(0, dt)，再扫所有 channel 调 fn(elapsed) → SetUniform。
    void             Tick(float dt) override;
    // procedural 没有"自然结束"——永远返回 false。调用方按需自己用 elapsed
    // 阈值判定。
    bool             IsFinished() const noexcept override;
    std::string_view BackendName() const noexcept override;

    // 当前累计时间（秒）。供调试 / 与外部时间源同步。
    float       ElapsedSeconds() const noexcept;
    std::size_t ChannelCount() const noexcept;

    // 把 elapsed 拨回某个值。用于"重启 channel 时序"——典型场景：换状
    // 态时让 dissolve 从头开始。仅改 elapsed，不影响 channel 列表。
    void ResetElapsed(float seconds = 0.0f) noexcept;

private:
    // 类型擦除的 channel：把"时间 → SetUniform"这一对压成一个虚表入口。
    struct IChannel
    {
        std::string name;
        explicit IChannel(std::string n) : name(std::move(n)) {}
        virtual ~IChannel() = default;

        virtual void Apply(Render::MaterialInstance& mi, float t) const = 0;
    };

    template <typename T>
    struct TypedChannel final : IChannel
    {
        std::function<T(float)> fn;
        TypedChannel(std::string n, std::function<T(float)> f)
            : IChannel(std::move(n))
            , fn(std::move(f))
        {
        }
        void Apply(Render::MaterialInstance& mi, float t) const override
        {
            // 这里依赖 MaterialInstance 提供的 SetUniform 重载集——T 不在
            // 重载集合里，编译失败。
            mi.SetUniform(name, fn(t));
        }
    };

    Render::MaterialInstance*              mpTarget{nullptr};
    float                                  mElapsedSeconds{0.0f};
    std::vector<std::unique_ptr<IChannel>> mChannels;
};

template <typename T>
void ProceduralAnimator::AddChannel(std::string_view name, std::function<T(float)> fn)
{
    mChannels.emplace_back(std::make_unique<TypedChannel<T>>(std::string{name}, std::move(fn)));
}

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_PROCEDURAL_ANIMATOR_H
