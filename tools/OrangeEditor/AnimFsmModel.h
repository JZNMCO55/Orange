#ifndef ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_MODEL_H
#define ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_MODEL_H

// ---------------------------------------------------------------------------
// AnimFsmModel —— .anim_fsm 文件的可序列化编辑期数据模型 POD。
//
// 与运行时 AnimationStateMachine 的分工：
//   * AnimationStateMachine（include/orange/engine/animation/AnimationStateMachine.h）
//     是 flat-weighted FSM 运行时容器；State 含 std::function callback，
//     Transition 含 std::function predicate —— 不可序列化。
//   * 本文件定义的 Editable* POD 是 .anim_fsm 文件落盘的纯数据形态；
//     编辑器侧维护，加载到运行时由中间层把 POD 翻译成
//     AnimationStateMachine.AddState / AddTransition 调用（含 callback /
//     predicate 桥接）。
//
// 设计意图：std::function 不可 introspect / 序列化是引擎运行时刻意接受
// 的代价（见 AnimationStateMachine.h 头注释 + Orange-Wiki
// `wiki/techniques/animation/animation-state-machine.md`）；编辑器侧维护
// **独立的**可序列化模型，与运行时模型隔离，避免污染引擎公共面。
//
// 本期 POD 字段限制为"绝对确定的字段"：state name + clip 名 + layout 位置
// + transition 端点 + initialState。Condition DSL 字段留 c2-6 ADR-005 决
// 策后扩展（schema v1 → v2 bump，与 MaterialFileIO v1.0 → v1.1 schema 演
// 化同款）。
//
// 与文件格式的关系：本头不涉及 IO；序列化由同目录 AnimFsmFileIO.{h,cpp}
// 落地（c2-1）。Inspector 接管由 plugin/AnimFsmAssetInspectorPlugin 落
// 地（c2-2）。
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

namespace Orange::Editor::AnimFsm
{

// 单条 state 的可序列化表示。
//   name      —— state 唯一标识。运行时翻译时
//                AnimationStateMachine.AddState 喂这个 name 作为索引；
//                同名 state 在同一 FSM 内不允许（reader 端 c2-1 会检查
//                + reject）。
//   clipName  —— 触发该 state 时 backend 想播放的 clip / 通道名。c3 落地
//                DragonBones 浏览后这里有机会改为 AssetRef<AnimationClip>；
//                c4 落地 Procedural channel 配置后这里映射到 channel 名。
//                c2 期允许为空（编辑器仅画拓扑、不绑 clip 也能保存）。
//   layoutX/Y —— 节点在图编辑器画布上的位置；**不**消费在运行时，只
//                为下次打开 .anim_fsm 时恢复编辑器布局。c2-3 节点图绘制
//                pass 落地时定义具体单位。
struct EditableState
{
    std::string name;
    std::string clipName;
    float       layoutX{0.0f};
    float       layoutY{0.0f};
};

// 单条 transition 的可序列化表示。
//   fromState / toState —— 必须是同一 FSM 内 states[] 已注册的 name；
//                          reader 端 c2-1 做跨引用检查，未命中跳过 +
//                          stderr 警告（不抛、不崩，与项目其它 IO 同节奏）。
//
// Condition 字段刻意留空：std::function predicate 的不可序列化解法
// （EditableCondition DSL 或引擎端 ConditionExpr 数据结构）属 c2-6
// ADR-005 决策范围；本 c2-0 / c2-1 / c2-2 阶段不预设字段，避免 ADR
// 决策被字段形态绑死。schema v1 不含 condition；c2-6 ADR-005 决策后
// bump 到 v2 扩展，与 MaterialFileIO v1.0 → v1.1 schema 演化同款。
struct EditableTransition
{
    std::string fromState;
    std::string toState;
};

// .anim_fsm 文件的运行时表示（编辑器持有 + 落盘 / 加载的根对象）。
//   states        —— 当前 FSM 全部 state；顺序 = 编辑器加节点的顺序，
//                    runtime 翻译时按此顺序 AddState。
//   transitions   —— 当前 FSM 全部 transition；顺序 = 编辑器加边的顺序
//                    = runtime 求值顺序，与 AnimationStateMachine.AddTransition
//                    "按注册顺序顺序求值" 语义对齐。
//   initialState  —— 启动状态名。空 / 未在 states[] 内 → reader 端 c2-1
//                    报警但仍 load（runtime 翻译方按 CurrentState() 验证）。
struct EditableStateMachine
{
    std::vector<EditableState>      states;
    std::vector<EditableTransition> transitions;
    std::string                     initialState;
};

}  // namespace Orange::Editor::AnimFsm

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_MODEL_H
