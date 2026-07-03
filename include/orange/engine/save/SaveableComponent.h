#ifndef ORANGE_ENGINE_SAVE_SAVEABLE_COMPONENT_H
#define ORANGE_ENGINE_SAVE_SAVEABLE_COMPONENT_H

// ---------------------------------------------------------------------------
// SaveableComponent —— "这一 entity 属于玩家进度，应被存档"的纯标记。
//
// SaveGameSystem::Save 只遍历挂着该 component 的实体，写它们持有的（已
// 注册到 SaveGameRegistry 的）component 字段。其它实体（关卡 prop、装
// 饰物、相机等）不入存档——它们由 Scene 序列化负责持久化。
//
// SaveGameSystem::Load 创建新实体后会自动 attach 一份 SaveableComponent，
// 让加载后的世界紧接着再 Save 一次仍能 round-trip（对称）。
//
// 该 component 不携带任何字段——是否入存档由"是否 attach 该 component"
// 决定，更细粒度（如 slot tag、autosave-only tag）若未来需要再扩。
// ---------------------------------------------------------------------------

namespace Orange::Engine::Save
{

    struct SaveableComponent
    {
    };

} // namespace Orange::Engine::Save

#endif // ORANGE_ENGINE_SAVE_SAVEABLE_COMPONENT_H
