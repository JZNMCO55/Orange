#ifndef ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_FILE_IO_H
#define ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_FILE_IO_H

// ---------------------------------------------------------------------------
// AnimFsmFileIO —— .anim_fsm 文件的 v1.0 schema 读写 helper（编辑器侧）。
//
// Schema v1.0（namespace: editor/anim_fsm）：
//   {
//     "schemaVersion":  {"namespace":"editor/anim_fsm","major":1,"minor":0},
//     "initialState":   "idle",
//     "states": [
//       {"name":"idle", "clipName":"idle", "layout":[0.0, 0.0]},
//       {"name":"walk", "clipName":"walk", "layout":[200.0, 0.0]}
//     ],
//     "transitions": [
//       {"from":"idle", "to":"walk"},
//       {"from":"walk", "to":"idle"}
//     ]
//   }
//
// Reader 端跨引用检查：
//   * transition.from / transition.to 未命中 states[].name → stderr 警告
//     + 跳过该条 transition（与 MaterialFileIO 跳过坏 uniform 同节奏）
//   * initialState 未命中 states[].name → stderr 警告 + 仍保留 string
//     原值（runtime 翻译方按 AnimationStateMachine::CurrentState() 验证）
//   * 同名 state → 整个文件 reject（返回 nullopt）；与同名键的语义模糊
//     性比起来直接 reject 更安全
//
// 本 helper 不在引擎公共面 —— 服务 OrangeEditor 工具链（Inspector
// AnimFsmAssetInspectorPlugin Save 路径 + 加载 .anim_fsm 翻译到 runtime
// AnimationStateMachine 的中间层）。引擎侧公共面只到 AnimationStateMachine
// 运行时 API，schema 解析永远属于消费方（与"公共头无裸 nlohmann::json"
// 纪律一致）。
//
// Condition DSL 在 schema v1.0 内**不存在**字段位置；c2-6 ADR-005 决策
// 后 bump 到 v1.1 扩展 transition 段（与 MaterialFileIO v1.0 → v1.1
// schema 演化同款）。
// ---------------------------------------------------------------------------

#include "AnimFsmModel.h"

#include <optional>
#include <string>

namespace Orange::Editor::AnimFsm
{

// 读取 .anim_fsm 文件。失败场景（返回 std::nullopt + stderr 记录）：
//   * 文件不存在 / JSON 解析失败
//   * schemaVersion 缺失 / namespace 不匹配 / major 不匹配（前向兼容 minor）
//   * states[] 段有同名 state（schema 不允许 ambiguous index）
//
// 成功路径包括"几乎所有非致命的局部错误都跳过 + 警告"——典型如 transition
// 端点不命中 states[]、缺字段 default 为空。意图与 MaterialFileIO 一致：
// 编辑器期间应 reload 友好，不因小瑕疵让用户失去全部进度。
std::optional<EditableStateMachine> ReadAnimFsmFile(const std::string& path);

// 写 .anim_fsm 文件。永远以 v1.0 schema 写盘。writer 不做 states 同名 /
// transition 端点跨引用合法性检查（信任 caller；写盘后再走 reader 验证
// 即可暴露 schema 违例）。失败 → stderr 记录 + 返回 false。
bool WriteAnimFsmFile(const std::string& path, const EditableStateMachine& data);

}  // namespace Orange::Editor::AnimFsm

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_FILE_IO_H
