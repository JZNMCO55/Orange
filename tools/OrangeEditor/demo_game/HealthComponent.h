#ifndef ORANGE_EDITOR_DEMO_GAME_HEALTH_COMPONENT_H
#define ORANGE_EDITOR_DEMO_GAME_HEALTH_COMPONENT_H

// 演示用游戏侧自定义组件 —— 验证 extraSerializers 扩展点的完整 round-trip。
// 生产游戏代码不属于编辑器仓库；此文件仅作 OrangeEditor v0.3 c2 验收用例。

#include <orange/engine/scene/ComponentSerializerEntry.h>

namespace DemoGame
{

    struct HealthComponent
    {
        int hp{100};
        int maxHp{100};
    };

    // 在 ComponentSchemaRegistry 注册 Inspector schema（hp / maxHp 两个 int 字段）。
    // main 启动期调用一次。
    void RegisterHealthComponentSchema();

    // 返回可填入 extraSerializers 的 ComponentSerializerEntry（const ref，lifetime = program）。
    const Orange::Engine::Scene::ComponentSerializerEntry& GetHealthSerializerEntry();

} // namespace DemoGame

#endif // ORANGE_EDITOR_DEMO_GAME_HEALTH_COMPONENT_H
