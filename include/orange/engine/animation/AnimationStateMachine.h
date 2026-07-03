#ifndef ORANGE_ENGINE_ANIMATION_ANIMATION_STATE_MACHINE_H
#define ORANGE_ENGINE_ANIMATION_ANIMATION_STATE_MACHINE_H

// ---------------------------------------------------------------------------
// AnimationStateMachine —— 轻量 flat-weighted FSM。
//
// 按 `vendor/Orange-Wiki/wiki/techniques/animation/animation-state-machine.md`
// 的"两种 ASM 架构"，当前只交付最简的 flat weighted-average 入口（状态
// 名 + 进入/退出回调 + 条件过渡）；blend tree 留待后续扩展。状态本身
// 的 cross-fade 由各 backend 自管（DragonBones 切 anim 时设 fade 时长，
// Procedural 后端不需要 fade）。
//
// 状态节点：用 `std::string` 名字索引；进入 / 退出回调通过 `std::function`
// 传入。
//
// 过渡条件有两条**等价的 API 路径**：
//   * 旧路径 `ConditionFn`（lambda 驱动）—— `bool(const StateContext&)`
//     predicate；由调用方写。**保留向后兼容**——v0.1~v0.6 期所有游戏侧
//     lambda-driven 代码零改动。
//   * 新路径 `ConditionExpr` list（数据驱动，v0.7 c2-7 落，ADR-005）——
//     `{paramName, op, threshold}` 三元组的 vector；多条 AND 组合；可序
//     列化到 `.anim_fsm` 文件，编辑器图编辑模式消费此路径。
//
// 两条路径共享 transitions vector：新 API 内部把 `vector<ConditionExpr>`
// 包装成 ConditionFn lambda 存入同一容器，Tick 只 evaluate ConditionFn。
//
// **不**支持：
//   * blend tree（待后续扩展）；
//   * layered ASM（后续随 IK / additive 一起做）；
//   * 复合 condition 表达式（OR/NOT/嵌套）—— AND 列表 + transition 复
//     制可模拟，与 Unity / Lumix 工业惯例一致；
//   * 状态间 cross-fade 时长本身（每个 backend 自管，在 OnEnter 回调
//     里调 backend.Play(animName, fadeIn) 即可）。
//
// 用法（旧 lambda 路径）：
//   AnimationStateMachine fsm;
//   fsm.AddState("idle", [&] { backend.Play("idle", 0.2f); });
//   fsm.AddState("walk", [&] { backend.Play("walk", 0.2f); });
//   fsm.AddTransition("idle", "walk", [&](const auto&) { return inputAxis != 0; });
//   fsm.SetInitialState("idle");
//   fsm.Tick(dt);
//
// 用法（新数据路径）：
//   fsm.RegisterParameter("isMoving", ParameterType::Bool);
//   fsm.AddTransition("idle", "walk", {{"isMoving", ConditionOp::If, true}});
//   fsm.AddTransition("walk", "idle", {{"isMoving", ConditionOp::IfNot, false}});
//   fsm.SetInitialState("idle");
//   // 每帧：
//   fsm.SetParameterBool("isMoving", inputAxis != 0);
//   fsm.Tick(dt);
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Orange::Engine::Animation
{

    // ---------------------------------------------------------------------------
    // Parameter table —— v0.7 c2-7（ADR-005）
    // ---------------------------------------------------------------------------

    enum class ParameterType : std::uint8_t
    {
        Bool,
        Int,
        Float,
        // Trigger：一次性 bool —— 调 SetTrigger() 设为 true，Tick 内某条
        // transition fire 后自动 reset 为 false（与 Unity Animator Trigger 一致）。
        Trigger,
    };

    struct Parameter
    {
        ParameterType type{ParameterType::Bool};
        // 值用 variant 包装；indices 与 ParameterType 对齐：
        //   Bool / Trigger → variant index 0 (bool)
        //   Int            → variant index 1 (std::int32_t)
        //   Float          → variant index 2 (float)
        std::variant<bool, std::int32_t, float> value;
    };

    // ---------------------------------------------------------------------------
    // Condition expression —— v0.7 c2-7（ADR-005）
    // ---------------------------------------------------------------------------

    enum class ConditionOp : std::uint8_t
    {
        // Bool / Trigger 参数：仅检查参数布尔值；threshold 字段不消费。
        If,    // 等价 paramValue == true
        IfNot, // 等价 paramValue == false
        // Int / Float 参数：比较 paramValue OP threshold。
        // Bool/Trigger 走数值比较时按 false=0/true=1 widening。
        Greater,
        Less,
        Equal,
        NotEqual,
        GreaterEqual,
        LessEqual,
    };

    struct ConditionExpr
    {
        std::string paramName;
        ConditionOp op{ConditionOp::If};
        // threshold 仅在 Greater/Less/Equal/NotEqual/GreaterEqual/LessEqual
        // 时消费。If/IfNot 时本字段不读，可置默认。
        std::variant<bool, std::int32_t, float> threshold;
    };

    // ---------------------------------------------------------------------------
    // StateContext —— 过渡条件求值时的当前帧上下文
    // ---------------------------------------------------------------------------

    struct StateContext
    {
        std::string_view currentStateName;     // 当前活跃状态名（静态生命周期至 Tick 返回）
        float            elapsedSeconds{0.0f}; // 进入当前状态以来累计秒数
        // c2-7 新增：指向 AnimationStateMachine 内部 parameter table 的只读
        // 指针。旧 ConditionFn lambda 可忽略此字段（lambda 闭包自己捕获参数）；
        // 新 AddTransition(ConditionExpr) 内部包装的 lambda 依赖此字段。
        // null 时数据驱动 condition 全部 evaluate 为 false（防御性 no-op）。
        const std::unordered_map<std::string, Parameter>* parameters{nullptr};
    };

    class ORANGE_ENGINE_API AnimationStateMachine
    {
    public:
        using OnEnterFn   = std::function<void()>;
        using OnExitFn    = std::function<void()>;
        using ConditionFn = std::function<bool(const StateContext&)>;

        AnimationStateMachine();
        ~AnimationStateMachine();

        AnimationStateMachine(const AnimationStateMachine&)            = delete;
        AnimationStateMachine& operator=(const AnimationStateMachine&) = delete;

        AnimationStateMachine(AnimationStateMachine&&) noexcept;
        AnimationStateMachine& operator=(AnimationStateMachine&&) noexcept;

        // 注册一个新状态。重名 → 覆盖回调（不报错；与 MaterialSystem 的
        // AlreadyExists 拒绝节奏不同——这里 FSM 是调用方手写的、覆盖更
        // 友好）。onEnter / onExit 任一可空（默认 no-op）。
        void AddState(std::string_view name,
                      OnEnterFn        onEnter = {},
                      OnExitFn         onExit  = {});

        // 注册一条 from → to 过渡边（旧 lambda 路径，保留向后兼容）。
        // 同 (from, to) 多次注册会追加 —— Tick 时按注册顺序逐个 evaluate
        // condition，第一条返回 true 的 fire。空 condition 会被拒绝（避免后续
        // Tick 解引用空 std::function）。
        void AddTransition(std::string_view from,
                           std::string_view to,
                           ConditionFn      condition);

        // 注册一条 from → to 过渡边（新数据驱动路径，v0.7 c2-7 / ADR-005）。
        // conditions 是 AND 组合：全部 evaluate true 才 fire；空 vector
        // 等价"无条件过渡"——Tick 内一旦 from 是当前状态即 fire。内部包装
        // 成 ConditionFn lambda 存入同一 transitions 容器。
        void AddTransition(std::string_view           from,
                           std::string_view           to,
                           std::vector<ConditionExpr> conditions);

        // 设置初始状态。必须在第一次 Tick 之前调用一次。设置后立即调一次
        // 该状态的 onEnter。重复调用：先 onExit 旧状态 → 再 onEnter 新状态。
        // 状态名未注册 → no-op（不抛、不崩；调用方按 CurrentState() 验证）。
        void SetInitialState(std::string_view name);

        // 推进一帧：累加 elapsedSeconds、按当前状态的所有出向 transition 顺序
        // evaluate condition；命中 → onExit 旧 / onEnter 新 / elapsed 清零。
        // 一帧内可能连发多条过渡（例如 condition A 触发后 condition B 又满足），
        // 但同一帧内本函数仅 fire **一次过渡**——避免无限循环。
        // CurrentState 未设 / 没有任何 state 注册时 Tick 是 no-op。
        // **Trigger 参数 reset**：fire 一条 transition 后遍历所有 Trigger 类型
        // 参数 set 为 false（与 Unity Animator Trigger 语义一致）。
        void Tick(float dt);

        // 当前状态名。CurrentState 未设时返回空 string_view。
        std::string_view CurrentState() const noexcept;

        // 进入当前状态以来累计秒数。CurrentState 未设时返回 0。
        float ElapsedSeconds() const noexcept;

        // 已注册状态数 / 过渡边数（诊断 / 单测用）。
        std::size_t StateCount() const noexcept;
        std::size_t TransitionCount() const noexcept;

        // ---- Parameter table API（v0.7 c2-7 / ADR-005）---------------------

        // 注册一个 parameter。重名 → 覆盖（与 AddState 同款 forgiving 节奏；
        // 覆盖时重置 value 为对应 type 的零值）。
        void RegisterParameter(std::string_view name, ParameterType type);

        // 运行时 setter —— 游戏侧每帧根据 input / 逻辑更新；参数未注册 → no-op
        // + stderr 警告。类型不匹配 → 类型 widening（bool→int→float 自动），
        // 或 stderr 警告 + no-op（如 SetParameterFloat 到 Bool 参数）。
        void SetParameterBool(std::string_view name, bool value);
        void SetParameterInt(std::string_view name, std::int32_t value);
        void SetParameterFloat(std::string_view name, float value);
        // SetTrigger：等价 SetParameterBool(name, true)；Tick 内某条 transition
        // fire 后所有 Trigger 类参数自动 reset 为 false。
        void SetTrigger(std::string_view name);

        // 读 parameter（诊断 / 编辑器 / 命令 replay 用）；未注册 / 类型不匹配
        // 返回 default（false / 0 / 0.0f）。
        bool         GetParameterBool(std::string_view name) const noexcept;
        std::int32_t GetParameterInt(std::string_view name) const noexcept;
        float        GetParameterFloat(std::string_view name) const noexcept;

        // 已注册 parameter 数（诊断 / 单测用）。
        std::size_t ParameterCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> mpImpl;
    };

} // namespace Orange::Engine::Animation

#endif // ORANGE_ENGINE_ANIMATION_ANIMATION_STATE_MACHINE_H
