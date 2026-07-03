#ifndef ORANGE_ENGINE_SCENE_LAYER_COMPONENT_H
#define ORANGE_ENGINE_SCENE_LAYER_COMPONENT_H

// ---------------------------------------------------------------------------
// LayerComponent —— 把实体归属到某个 layer（"图层"）。
//
// 字段语义：
//   * layerId —— 该实体所属的 layer 字符串 id。空字符串表示"使用 World
//                的默认 layer"，由 WorldPartition::DefaultLayerId() 解释。
//
// 设计取舍：为什么只放一个 string 字段？
//   * visible / displayName / source 等 layer 全局属性属于
//     WorldPartition 的 manifest，不是每个实体的拷贝；放在 component 上
//     会出现 N 份重复且容易撞 inconsistency。
//   * 用 string 而不是 hash / enum：layer 数量天然少（几个到几十），
//     可读性 + VCS friendly > cache locality 的占比远超 hot-path 成本。
//   * 实体没挂 LayerComponent 视为"在 default layer 上"，由 Render /
//     Physics / Save 路径在 lookup 时统一兜底，避免每条 CreateEntity 强
//     制开发者额外挂一次。
//
// 与 WorldPartition 的关系：
//   * LayerComponent.layerId 是单向引用——WorldPartition 是 manifest 的
//     权威；component 字段只是按 id 查表。manifest 里没有的 id 视为
//     "孤儿 layer"——按 default layer 处理 + warn。
//
// 详见 vendor/Orange-Wiki/wiki/concepts/gameplay/game-world-editor.md
// §核心功能 5（Layers）+ §陷阱 4（Chunk 粒度与 VCS 冲突）。
// ---------------------------------------------------------------------------

#include <string>

namespace Orange::Engine::Scene
{

    struct LayerComponent
    {
        std::string layerId;
    };

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_LAYER_COMPONENT_H
