#ifndef ORANGE_ENGINE_SCENE_SCENE_SERIALIZATION_H
#define ORANGE_ENGINE_SCENE_SCENE_SERIALIZATION_H

// ---------------------------------------------------------------------------
// Scene::Serialization —— 把 World（实体 + 内置组件）写到 .scene.json 与
// 从 .scene.json 读回来。
//
// 公共面只有两个自由函数：
//
//   Save(world, path)        把 world 当前状态序列化到 path（覆盖写）。
//   Load(path, &world)       把 path 反序列化到 world（追加，不清空）。
//
// 设计要点（详见 docs/design-plan.md 中关于 Scene 序列化的设计描述）：
//
// * **JSON schema v1**（namespace="scene/world"）顶层结构：
//
//       {
//         "schemaVersion": { "namespace": "scene/world", "major": 1, "minor": 0 },
//         "entities": [
//           {
//             "id": <持久 int, 0-based 顺序分配>,
//             "components": {
//               "Transform": { "position": [x,y,z], "rotation": [x,y,z,w], "scale": [x,y,z] },
//               "Hierarchy": { "parent": <id 或 -1>, "firstChild": ..., "nextSibling": ..., "prevSibling": ... },
//               "Name": "..."
//             }
//           },
//           ...
//         ]
//       }
//
// * **持久 entity ID 与 EnTT entity 解耦**：序列化时按 view 顺序给每个
//   实体分配 0..N-1 的持久 ID，写到 JSON。Hierarchy 的 parent /
//   firstChild / 等字段全部用持久 ID 引用。Load 时建立持久 ID → 新
//   World CreateEntity() 出来的 Entity 的双向映射，先全部建实体再回
//   填 Hierarchy 引用——保证 Hierarchy 字段永远指向已存在 entity。
//
// * **Forward-compat**：Read 路径遇到当前版本不识别的 component 名
//   key → warn + skip，不视为 fatal，让旧版引擎能开新版 scene。
//
// * **Schema version mismatch / 损坏 JSON**：返回 Error 时保证 World
//   保持原状（不部分写入）。
//
// * **当前覆盖范围**：Transform / Hierarchy / Name 三件套；其余内置
//   组件由 Render / Physics / Animation 模块按需往同一调度表里追加。
//
// 公共头不引入 Serialization.h——读 / 写实现自然内部要用，但调用方
// 只看到 Result + std::filesystem::path 接口，不被 nlohmann 感染。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>

#include <string_view>

namespace Orange::Engine
{

class World;

}  // namespace Orange::Engine

namespace Orange::Engine::Asset
{

class AssetRegistry;

}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Scene
{

// 把 `world` 写到 `path`。覆盖目标文件。
//
// `assetRegistry` 可空：仅当有组件持有 AssetHandle（譬如 Renderable 的
// mesh）时才需要——序列化层用 registry 反查"handle → 资源路径"。传
// nullptr 时这类组件落空 path 并 warn，整 scene 仍可保存。
ORANGE_ENGINE_API Result<void, ResultCode> Save(const World& world,
                                                std::string_view path,
                                                const Asset::AssetRegistry* assetRegistry = nullptr);

// 从 `path` 读取 scene 数据，把所有实体 + 组件追加到 `world` 上。
// 不清空 world——调用方若需要"完全替换当前关卡"，自己先构造一个新
// World 再把读取结果合进去。
//
// `assetRegistry` 可空：用于把 scene 文件里的资源路径解析回 AssetHandle。
// 传 nullptr 时持有 AssetHandle 的组件被装上空 handle 并 warn，仍正
// 常 attach 到 entity——entity / hierarchy 等 pure-data 字段不受影响。
//
// 失败语义：
//   * 文件不存在 / IO 错误     → IoError
//   * JSON 解析失败             → InvalidArgument（World 不被改动）
//   * schemaVersion 不兼容      → SchemaMismatch（World 不被改动）
//   * 必填字段缺失 / 类型错误   → InvalidArgument（World 不被改动）
//
// 未识别的 component 名 → 仅记录 warning 后继续（forward-compat）。
ORANGE_ENGINE_API Result<void, ResultCode> Load(std::string_view path,
                                                World& world,
                                                Asset::AssetRegistry* assetRegistry = nullptr);

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_SCENE_SERIALIZATION_H
