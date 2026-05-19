#ifndef ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_FILE_IO_H
#define ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_FILE_IO_H

// ---------------------------------------------------------------------------
// AnimFsmFileIO —— .anim_fsm 文件 schema v1.1 读写 helper（编辑器侧）。
//
// Schema v1.0（namespace: editor/anim_fsm，c2-2 落）：
//   {
//     "schemaVersion":  {"namespace":"editor/anim_fsm","major":1,"minor":0},
//     "initialState":   "idle",
//     "states":      [ {name, clipName, layout: [x,y]} ],
//     "transitions": [ {from, to} ]
//   }
//
// Schema v1.1（ADR-005 / c2-7-B，本期）扩展：
//   * 顶层加 "parameters" 数组：每条 { name, type, default: {type, value} }
//   * transition 加 "conditions" 数组：每条 { paramName, op, threshold }
//   * minor 从 0 → 1（前向兼容：v1.0 文件 reader 视 parameters / conditions 为空）
//
//   {
//     "schemaVersion":  {"namespace":"editor/anim_fsm","major":1,"minor":1},
//     "initialState":   "idle",
//     "parameters": [
//       {"name":"isMoving","type":"bool","default":{"type":"bool","value":false}},
//       {"name":"speed",   "type":"float","default":{"type":"float","value":0.0}}
//     ],
//     "states":      [ ... ],
//     "transitions": [
//       {
//         "from":"idle","to":"walk",
//         "conditions": [
//           {"paramName":"isMoving","op":"if",
//            "threshold":{"type":"bool","value":false}}
//         ]
//       }
//     ]
//   }
//
// type 字符串集（与引擎 enum 对应）：
//   * ParameterType: "bool" / "int" / "float" / "trigger"
//   * ConditionOp:   "if" / "ifNot" / "greater" / "less" / "equal" /
//                    "notEqual" / "greaterEqual" / "lessEqual"
//   * threshold value 子对象类型: "bool" / "int" / "float"（trigger
//     不出现 —— trigger 是 parameter type 不是 value type）
//
// Reader 端跨引用检查：
//   * 同名 state → 整文件 reject（c2-2 既有规则）
//   * transition.from / .to / condition.paramName 未命中 → 跳过该条 +
//     stderr 警告（不抛、不崩，与 v1.0 同节奏）
//   * initialState 未命中 → 仍 load + stderr 警告
// ---------------------------------------------------------------------------

#include "AnimFsmModel.h"

#include <optional>
#include <string>

namespace Orange::Editor::AnimFsm
{

// 读取 .anim_fsm 文件。失败场景（返回 std::nullopt + stderr 记录）：
//   * 文件不存在 / JSON 解析失败
//   * schemaVersion 缺失 / namespace 不匹配 / major 不匹配（前向兼容 minor）
//   * states[] 段有同名 state
//
// v1.0 文件兼容：缺 parameters / transitions[].conditions 视为空。
std::optional<EditableStateMachine> ReadAnimFsmFile(const std::string& path);

// 写 .anim_fsm 文件。永远以 v1.1 schema 写盘（即使 parameters / conditions
// 为空也写空数组 —— reader 兼容路径与之等价）。失败 → stderr 记录 + 返回 false。
bool WriteAnimFsmFile(const std::string& path, const EditableStateMachine& data);

}  // namespace Orange::Editor::AnimFsm

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_ANIM_FSM_FILE_IO_H
