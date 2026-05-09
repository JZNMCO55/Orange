#ifndef ORANGE_ENGINE_SCENE_NAME_COMPONENT_H
#define ORANGE_ENGINE_SCENE_NAME_COMPONENT_H

// ---------------------------------------------------------------------------
// NameComponent —— 实体的人类可读名字。
//
// 不参与任何引擎语义判断（不充当 lookup key、不做层级路径），纯粹给
// 编辑器 entity tree / 调试 overlay / 关卡序列化里的 readability 用。
// 名字允许重复，也允许为空——空字符串视为"未命名"，编辑器 UI 可回
// 退到 "Entity #<id>" 形态显示。
//
// 当前阶段仅一个 std::string 字段，刻意不再加 tag / category 等概念：
// 那些是 component-as-tag 模型适合解决的问题，不应该塞进 Name 里把
// 语义和命名混在一起。
// ---------------------------------------------------------------------------

#include <string>

namespace Orange::Engine::Scene
{

struct NameComponent
{
    std::string name;
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_NAME_COMPONENT_H
