#ifndef ORANGE_ENGINE_SCENE_HIERARCHY_COMPONENT_H
#define ORANGE_ENGINE_SCENE_HIERARCHY_COMPONENT_H

// ---------------------------------------------------------------------------
// HierarchyComponent —— 把实体串成 parent / sibling 链。
//
// 用 "parent + 兄弟链" 的结构，而不是 "parent + children 数组"：
//   * children 数组在 ECS 列存储下放不进 component（变长字段会破坏
//     archetype 的 SoA 布局）；
//   * 兄弟链每个节点固定四个 Entity 字段，定长、可批量遍历，
//     archetype 友好。
//
// 字段语义：
//   * parent       —— 父节点；根节点的 parent 为 Entity::Invalid()。
//   * firstChild   —— 第一个孩子；无孩则为 Invalid。
//   * nextSibling  —— 同一父下的下一个兄弟；末尾兄弟为 Invalid。
//   * prevSibling  —— 上一个兄弟；首兄弟为 Invalid。双向链便于 O(1)
//                     从父子结构中摘除节点，无需扫整条兄弟链。
//
// 当前阶段不在 component 内缓存 child count / depth；那些都是按需计
// 算量，缓存会引入 invalidation 烦扰。后续若 profiling 表明热路径需
// 要，再考虑加缓存字段（必然伴随 schema_version 升级）。
//
// 详见 vendor/Orange-Wiki/wiki/concepts/gameplay/component-model.md
// 关于"组合优于继承 / pure-component"的讨论。
// ---------------------------------------------------------------------------

#include <orange/engine/scene/Entity.h>

namespace Orange::Engine::Scene
{

    struct HierarchyComponent
    {
        Engine::Entity parent{Engine::Entity::Invalid()};
        Engine::Entity firstChild{Engine::Entity::Invalid()};
        Engine::Entity nextSibling{Engine::Entity::Invalid()};
        Engine::Entity prevSibling{Engine::Entity::Invalid()};

        // 根序：仅根节点（parent==Invalid）有意义——根不在兄弟链里，用 sortIndex
        // 决定根之间的显示/遍历顺序（小在前；相同则退化到 entity id 序）。非根节点
        // 忽略此字段（其顺序由兄弟链决定）。裸数据，由编辑器维护（引擎不做图操作，
        // 见 EditorHierarchy）；详见 ADR-014。
        int sortIndex{0};
    };

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_HIERARCHY_COMPONENT_H
