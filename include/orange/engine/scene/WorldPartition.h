#ifndef ORANGE_ENGINE_SCENE_WORLD_PARTITION_H
#define ORANGE_ENGINE_SCENE_WORLD_PARTITION_H

// ---------------------------------------------------------------------------
// WorldPartition —— layer manifest 的权威持有者 + 可见性 / 元数据查询入口。
//
// World 自身只关心实体 + 组件存储；"哪些 layer 存在 / 每个 layer 的显示
// 名 / 是否 visible / 来自哪个磁盘 source 文件" 都集中在这里。
//
// 与 LayerComponent 的关系：
//   * Entity 通过 LayerComponent.layerId 单向引用 WorldPartition 的 layer
//     条目；没挂 LayerComponent 的实体视为属于 DefaultLayerId()。
//   * WorldPartition 不强制反向索引 layer → entities；要遍历某 layer 的
//     所有实体走 EnTT view + 过滤即可（layer 数量天然少，O(N) 可接受；
//     真正瓶颈再说）。
//
// 与 SceneSerialization 的关系：
//   * 单文件 (.scene.json) 模式：LayerComponent.layerId 作为常规字段写
//     在 entity 的 components 段；manifest 内嵌在顶层 "layers" 段。
//   * 多文件 (.scene.manifest.json + 每 layer 一 .scene.json) 模式：
//     manifest 文件存 layer 元数据 + per-layer source 路径；entity 数据
//     按 layer 拆到独立 .scene.json。LayerInfo::source 字段记录该 layer
//     来自哪个文件，Save / Load 走 manifest 驱动的拆分。
//
// 多文件落盘的设计意图见 Orange-Wiki
// `concepts/gameplay/game-world-editor.md` §陷阱 4：
//   "Chunk 粒度与 VCS 冲突 —— per-layer 文件是工程缓解方案。"
//
// 默认 layer：构造时自动注册一条 "default" layer（visible=true）。这是
// 不可删除的兜底——RemoveLayer("default") 立即返回 false 不动；保证任
// 何"被释放层的孤儿"都能落到一个合法 layer 上。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/scene/Entity.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Orange::Engine
{
class World;
}

namespace Orange::Engine::Scene
{

// 一条 layer 的元数据。**仅 manifest 持有**——entity 上的
// LayerComponent 只引用 id，不复制这些字段。
struct LayerInfo
{
    std::string id;            // 唯一 key；与 LayerComponent.layerId 对应
    std::string displayName;   // 编辑器 UI 显示；空字符串视为"等同 id"
    bool        visible{true}; // false → Render / Physics 跳过本 layer 的 entity

    // 多文件序列化时该 layer 的 source 文件相对路径（manifest 所在目录
    // 为 base）。单文件模式下留空字符串。Load 路径用它决定从哪个文件
    // 反序列化本 layer 的 entity；Save 路径反向写出。
    std::string source;
};

class ORANGE_ENGINE_API WorldPartition
{
public:
    WorldPartition();
    ~WorldPartition();

    WorldPartition(const WorldPartition&)            = delete;
    WorldPartition& operator=(const WorldPartition&) = delete;

    WorldPartition(WorldPartition&&) noexcept;
    WorldPartition& operator=(WorldPartition&&) noexcept;

    // 默认 layer id —— 常量字符串 "default"。构造时自动注册，
    // RemoveLayer 拒绝删除。
    static std::string_view DefaultLayerId() noexcept;

    // ---- Layer manifest CRUD ----

    // 添加一条 layer。返回 false 表示 id 已存在（不覆盖原条目）。
    // 空 id 不被接受（return false）；其余字段允许空。
    bool AddLayer(LayerInfo info);

    // 删除 layer。返回 false 表示 id 不存在或试图删除 default。
    // 注意：本函数不动 World 内任何 LayerComponent.layerId；调用方若需
    // 让被删 layer 的 entity 改归 default，应在调用后自行遍历 World 重写
    // LayerComponent。设计上故意不在此处做隐式 mutate，避免 partition
    // 反向耦合 World 生命周期。
    bool RemoveLayer(std::string_view id);

    // 整体替换 manifest（顺序 + 内容）。常用于从 manifest 文件加载之后
    // 一次性灌入。会覆盖现有所有条目；调用后 default layer 仍然由本类
    // 自动补回（若入参里没有）。
    void ResetLayers(std::vector<LayerInfo> layers);

    bool HasLayer(std::string_view id) const noexcept;
    const LayerInfo* GetLayer(std::string_view id) const noexcept;
    LayerInfo*       GetLayer(std::string_view id) noexcept;

    // 顺序保持插入序——Editor UI 直接按这个序展示。
    const std::vector<LayerInfo>& GetLayers() const noexcept { return mLayers; }

    std::size_t LayerCount() const noexcept { return mLayers.size(); }

    // ---- Visibility ----

    // 不存在的 layer id 视为 visible=true（避免缺数据时整片场景消失）。
    bool IsLayerVisible(std::string_view id) const noexcept;
    void SetLayerVisible(std::string_view id, bool visible);

    // ---- Entity → layer 查询 ----

    // 读 entity 的 LayerComponent.layerId；没挂 / 空字符串 → DefaultLayerId()。
    // entity invalid 时返回 DefaultLayerId() + 静默（不视为 error）。
    std::string_view GetLayerOf(const World& world, Entity entity) const;

    // entity 应否被渲染 / 物理 tick。等价于
    // `IsLayerVisible(GetLayerOf(world, entity))`，让消费者只调一次。
    bool IsEntityVisible(const World& world, Entity entity) const;

    // 把 entity 挂到指定 layer（添加或修改 LayerComponent）。layerId 在
    // manifest 里不存在不阻塞——只是接受字符串，warn 由调用方关心；
    // 这条 API 故意保持薄，便于 deserialization 不依赖 manifest 已加载完。
    void SetLayerOf(World& world, Entity entity, std::string_view layerId);

private:
    // layer 顺序 + 数据；查找用 mIndex（O(1)）。
    std::vector<LayerInfo>                       mLayers;
    std::unordered_map<std::string, std::size_t> mIndex;

    void EnsureDefault();
    void RebuildIndex();
};

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_WORLD_PARTITION_H
