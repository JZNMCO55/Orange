#ifndef ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_MODEL_H
#define ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_MODEL_H

// ---------------------------------------------------------------------------
// AnimFsmModel —— .anim_fsm 文件的可序列化编辑期数据模型 POD。
//
// 与运行时 AnimationStateMachine 的分工：
//   * AnimationStateMachine（include/orange/engine/animation/AnimationStateMachine.h）
//     是 flat-weighted FSM 运行时容器；新 API（v0.7 c2-7 / ADR-005）已支
//     持 ConditionExpr + parameter table 的数据驱动 transition 路径
//   * 本头定义的 Editable* POD 是 .anim_fsm 文件落盘的纯数据形态；编辑
//     器侧维护，加载到运行时由中间层翻译成 AnimationStateMachine.AddTransition
//     (vector<ConditionExpr>) + RegisterParameter / SetParameter* 调用
//
// 设计意图：编辑器侧 EditableCondition / EditableParameter 与引擎侧
// ConditionExpr / Parameter 类型字段一一对应，但是独立 struct —— 让
// .anim_fsm schema 演化（v1.0 → v1.1 加 condition / parameters；未来
// v1.2 ...）与引擎 API 演化解耦。直接 use 引擎枚举（ConditionOp /
// ParameterType）避免双份 enum 同步负担。
//
// Schema 演化：
//   * v1.0（c2-2 落地）：state{name, clipName, layout} + transition{from, to}
//   * v1.1（c2-7 / ADR-005 / 本头）：上 + parameters[] + transition.conditions[]
// ---------------------------------------------------------------------------

#include <orange/engine/animation/AnimationStateMachine.h>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Orange::Editor::AnimFsm
{

    // 单条 state 的可序列化表示（v1.0+）。
    //   name      —— state 唯一标识；运行时翻译时 AnimationStateMachine.AddState
    //                喂这个 name 作为索引；同名 state 在同一 FSM 内不允许
    //                （reader 端 c2-2 检查 + reject）
    //   clipName  —— 触发该 state 时 backend 想播放的 clip / 通道名（c3 落
    //                DragonBones 浏览后这里有机会改为 AssetRef）。c2 期允许空
    //   layoutX/Y —— 节点在图编辑器画布上的位置；**不**消费在运行时，只为
    //                下次打开 .anim_fsm 时恢复编辑器布局
    struct EditableState
    {
        std::string name;
        std::string clipName;
        float       layoutX{0.0f};
        float       layoutY{0.0f};
    };

    // 单条 condition 的可序列化表示（v1.1 新增）。与引擎 ConditionExpr 一一对应。
    //   paramName —— 引用 parameters[] 内的某条 EditableParameter.name；reader
    //                端跨引用检查（不命中跳过 + 警告，与 transition 端点同节奏）
    //   op        —— 引擎 ConditionOp enum；序列化为字符串（"if" / "ifNot" /
    //                "greater" / ...）保证 schema 跨版本可读
    //   threshold —— 与 op 配合；If/IfNot 不消费此字段，Greater/Less/Equal/...
    //                按数值比较（bool/int/float widening 走引擎 AsFloat 路径）
    struct EditableCondition
    {
        std::string                              paramName;
        ::Orange::Engine::Animation::ConditionOp op{::Orange::Engine::Animation::ConditionOp::If};
        std::variant<bool, std::int32_t, float>  threshold;
    };

    // 单条 transition 的可序列化表示（v1.1 扩展 conditions 字段）。
    //   fromState / toState —— 必须是同一 FSM 内 states[] 已注册的 name
    //   conditions          —— v1.1 新增：多条 EditableCondition AND 组合
    //                          （空 vector = 无条件 transition，立即 fire）。
    //                          reader 端兼容 v1.0（缺 conditions 字段时视为空）
    struct EditableTransition
    {
        std::string                    fromState;
        std::string                    toState;
        std::vector<EditableCondition> conditions;
    };

    // 单条 parameter 的可序列化表示（v1.1 新增）。
    //   name         —— parameter 唯一标识；引用方 EditableCondition.paramName
    //                   按 name 索引
    //   type         —— 引擎 ParameterType enum；序列化为字符串（"bool" /
    //                   "int" / "float" / "trigger"）
    //   defaultValue —— 编辑器加载 .anim_fsm 时按此初始化运行时 parameter；
    //                   variant 索引按 type 选（Bool/Trigger → index 0 bool，
    //                   Int → index 1 int32_t, Float → index 2 float）
    struct EditableParameter
    {
        std::string                                name;
        ::Orange::Engine::Animation::ParameterType type{::Orange::Engine::Animation::ParameterType::Bool};
        std::variant<bool, std::int32_t, float>    defaultValue;
    };

    // .anim_fsm 文件的运行时表示（编辑器持有 + 落盘 / 加载的根对象）。
    //   states        —— 当前 FSM 全部 state；顺序 = 编辑器加节点顺序
    //   transitions   —— 当前 FSM 全部 transition；顺序 = 编辑器加边顺序
    //                    = runtime AddTransition 求值顺序
    //   parameters    —— v1.1 新增：FSM 全部 parameter；reader v1.0 兼容视为空
    //   initialState  —— 启动状态名
    struct EditableStateMachine
    {
        std::vector<EditableState>      states;
        std::vector<EditableTransition> transitions;
        std::vector<EditableParameter>  parameters;
        std::string                     initialState;
    };

} // namespace Orange::Editor::AnimFsm

#endif // ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_MODEL_H
