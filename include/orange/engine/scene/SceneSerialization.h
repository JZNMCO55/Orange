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
// * **自定义组件扩展**：游戏侧 / 编辑器侧可通过 SaveOptions /
//   LoadOptions 的 extraSerializers 字段追加自定义 ComponentSerializerEntry，
//   让 Scene::Save / Load 识别并序列化游戏侧 component。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/scene/ComponentSerializerEntry.h>

#include <span>
#include <string>
#include <string_view>

namespace Orange::Engine
{

class World;

}  // namespace Orange::Engine

namespace Orange::Engine::Asset
{

class AssetRegistry;

}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Physics
{

class PhysicsWorld;

}  // namespace Orange::Engine::Physics

namespace Orange::Engine::Animation
{

class AnimatorRegistry;

}  // namespace Orange::Engine::Animation

namespace Orange::Engine::Scene
{

class WorldPartition;

// Save / Load 的可选依赖打包。每条都是"持有 AssetHandle / backend 资源
// 的组件需要时才用得到"——传空时序列化层对相应组件走 graceful 退化，
// 不视为 fatal（详见各字段注释）。
//
// 用 designated initializer（C++20）调用：
//
//     auto opt = LoadOptions{
//         .assetRegistry    = &reg,
//         .physicsWorld     = &world,
//         .animatorRegistry = &animReg,
//         .extraSerializers = std::span{gameEntries},
//     };
//     Scene::Load(path, world, opt);
struct SaveOptions
{
    // 反查"AssetHandle → 资源路径"。空 → 持有 AssetHandle 的组件落
    // 空字符串 + warn。
    const Asset::AssetRegistry* assetRegistry{nullptr};

    // 按名字注册的 MaterialInstance 表（name → non-owning pointer）。
    // 供 RenderableComponent 把 materialInstance* 反查为 id 字符串写入 JSON。
    // 空 → materialInstance 写出空 id + warn。
    // 生命周期须覆盖 Save 调用期间。
    const std::unordered_map<std::string, Render::MaterialInstance*>* namedMaterialInstances{nullptr};

    // 游戏侧 / 编辑器侧自定义组件序列化器。条目 name 不得与内置组件名
    // 重复（重复时 Save 立即返回 AlreadyExists）。
    // span 指向的数据生命周期须覆盖 Save 调用期间。
    std::span<const ComponentSerializerEntry> extraSerializers{};
};

struct LoadOptions
{
    // 解析"资源路径 → AssetHandle"（典型：RenderableComponent.mesh）。
    // 空 → 相关 handle 留空 + warn。
    Asset::AssetRegistry* assetRegistry{nullptr};

    // 反序列化 RigidBody + Collider 时，用它注册 backend body（一次性
    // 把 rigid + collider 一并提交给 PhysicsWorld::AddBody）。空 → 组
    // 件仍 attach 但不绑定 backend，BodyHandle 留 Invalid。
    Physics::PhysicsWorld* physicsWorld{nullptr};

    // 反序列化 AnimatorComponent 时按 backend name 调 Create() 拿 IAnimator
    // 实例。空 / 未注册 → component 仍 attach 但 animator unique_ptr 为
    // nullptr。注：scene 仅持久化 backend 名字，具体的 skeleton / channel
    // 配置由 game 端在注册 factory 时 capture，不下钻到 schema。
    const Animation::AnimatorRegistry* animatorRegistry{nullptr};

    // 与 SaveOptions::namedMaterialInstances 相同表；Load 路径按 id 正向
    // 查找 MaterialInstance*，赋给 RenderableComponent::materialInstance。
    const std::unordered_map<std::string, Render::MaterialInstance*>* namedMaterialInstances{nullptr};

    // 游戏侧 / 编辑器侧自定义组件序列化器，同 SaveOptions::extraSerializers。
    std::span<const ComponentSerializerEntry> extraSerializers{};

    // 若非空：Load 路径在 attach 完所有 component 后，给本次新建且**没有**
    // LayerComponent 的 entity 强制挂上 LayerComponent{assignLayerId}。
    // LoadSplit 用它把"来自 layer X 的 source 文件" 自动归属到 X。
    // 单文件 Load 不需要时留空，保持向后兼容。
    std::string assignLayerId{};
};

// 把 `world` 写到 `path`。覆盖目标文件。
ORANGE_ENGINE_API Result<void, ResultCode> Save(const World& world,
                                                std::string_view path,
                                                const SaveOptions& options = {});

// 从 `path` 读取 scene 数据，把所有实体 + 组件追加到 `world` 上。
// 不清空 world——调用方若需要"完全替换当前关卡"，自己先构造一个新
// World 再把读取结果合进去。
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
                                                const LoadOptions& options = {});

// ---------------------------------------------------------------------------
// SaveSplit / LoadSplit —— 多文件 + manifest 序列化。
//
// 与单文件 Save / Load 关系：
//   * **完全独立**的 API；不破坏 v1.1 schema、不替换原 Save / Load。
//   * 调用方按"想要 per-layer 落盘"显式选这条路径；编辑器侧 v0.6 之后
//     会优先走 SaveSplit，减少多人编辑时的 VCS 冲突（Orange-Wiki
//     `concepts/gameplay/game-world-editor.md` §陷阱 4）。
//
// manifest 文件格式（独立 schema namespace `scene/manifest 1.0`）：
//
//   {
//     "schemaVersion": { "namespace": "scene/manifest", "major": 1, "minor": 0 },
//     "layers": [
//       { "id": "background", "displayName": "Background", "visible": true,
//         "source": "background.scene.json" },
//       { "id": "foreground", "displayName": "Foreground", "visible": true,
//         "source": "foreground.scene.json" }
//     ]
//   }
//
// per-layer .scene.json 文件复用 `scene/world` schema（当前 1.2）；仅
// 包含归属于该 layer 的 entity（实体内仍写 LayerComponent.id，确保
// "单文件 Load 也能恢复 layer 信息"）。
//
// source 路径 解析规则：以 `manifestPath` 所在目录为 base 解析相对路径。
//
// 失败语义：
//   * 任一 per-layer 文件 Save / Load 失败 → 整体返回失败；Load 走
//     internal rollback（已建实体回收）；Save 不保证回滚已写盘的中间
//     文件（与单文件 Save 同款约束——caller 应在临时目录写完再 rename）。
//
// `WorldPartition` 双向角色：
//   * Save 端：partition 提供 layer 列表 + 元数据（id / displayName /
//     visible / source）；GAP-2026-05-17 Save 路径会按 partition 的
//     layer 顺序遍历，每条 source 写一个 .scene.json。
//   * Load 端：partition 在 Load 完成后被 ResetLayers 灌入 manifest
//     里的 layer 列表（保留顺序），并且每条 LayerComponent.layerId 自
//     动按 source 归属。
// ---------------------------------------------------------------------------

ORANGE_ENGINE_API Result<void, ResultCode> SaveSplit(const World& world,
                                                     const WorldPartition& partition,
                                                     std::string_view manifestPath,
                                                     const SaveOptions& options = {});

ORANGE_ENGINE_API Result<void, ResultCode> LoadSplit(std::string_view manifestPath,
                                                     World& world,
                                                     WorldPartition& partition,
                                                     const LoadOptions& options = {});

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_SCENE_SERIALIZATION_H
