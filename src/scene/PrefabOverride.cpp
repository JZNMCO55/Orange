// PrefabOverride 实现 —— 实例↔模板字段级 override diff（只读，决策中立）。
//
// 三块：
//   1) SerializeEntityComponents —— 把单个 entity 的 present component 经已注册
//      的 ComponentSerializer 写成 JSON（复用 SaveImpl 的逐 entity 序列化循环 +
//      注册表，不重写各 serializer）。
//   2) 结构化 diff —— 把两份"entity component JSON"逐叶子路径比较，值不同 / 单侧
//      存在即一条 override（field 级粒度，design §5 问题 3）。
//   3) 配对入口 —— ComputeEntityOverrides（两 world 两 entity 底层）+
//      ComputeInstanceOverrides（经 templateEntityGuid + FindEntityByGuid 自动配对）。
//
// 头隔离：走 Core::Serialization 公共面（JsonWriter/JsonReader），无裸 nlohmann::json。

#include "orange/engine/scene/PrefabOverride.h"

#include "orange/engine/asset/PrefabAsset.h"
#include "orange/engine/core/Guid.h"
#include "orange/engine/core/Serialization.h"
#include "orange/engine/scene/EntityGuid.h"
#include "orange/engine/scene/PrefabInstanceComponent.h"
#include "orange/engine/scene/SceneSerialization.h"
#include "orange/engine/scene/World.h"

#include "scene/ComponentSerializers.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Scene
{
namespace
{

// component JSON 都挂在这个固定根下。单 entity 自包含，根名是什么不重要——
// 实例侧与模板侧用同一根，diff 时路径对齐即可。
constexpr std::string_view kComponentsRoot = "components";

// 路径拼接（与 ComponentSerializers.cpp::Join 同款，避免到处 string + "/"）。
std::string Join(std::string_view base, std::string_view leaf)
{
    std::string out;
    out.reserve(base.size() + 1 + leaf.size());
    out.append(base);
    out.push_back('/');
    out.append(leaf);
    return out;
}

// "身份/链接" component：实例与模板间本就该不同，不算业务 override，diff 前先排除。
//   * Guid             —— 实例换了全新 per-entity guid。
//   * PrefabInstance   —— sourcePrefabPath/instanceId/templateEntityGuid 等链接字段，
//     实例独有（模板侧根本没这个 component）。
//   * Hierarchy        —— parent/firstChild/sibling 互引用指向各自 world 的实体，
//     持久 id / guid 在两套独立 id 空间间天然不同；本期只做"同 entity 字段值
//     override"，结构性 override（加/删 entity / 改父子关系）留后续（design §5 问题 6）。
bool IsIdentityComponent(std::string_view componentName)
{
    return componentName == "Guid"
        || componentName == "PrefabInstance"
        || componentName == "Hierarchy";
}

// 把单个 entity 的 present component（排除身份/链接 component）经已注册的内置
// ComponentSerializer 写成 JSON 文本，挂在 kComponentsRoot 下。复用 SaveImpl 的
// 逐 entity 序列化循环：建一个只含本 entity（持久 id = 0）的最小 SaveContext，
// 遍历注册表，对 Has 命中的 component 调其 Write。
//
// 返回 JsonWriter::Dump() 的 JSON 文本。entity 无效 → 返回空对象文本（"{}"），
// diff 时与"无任何 component"等价。
//
// 注意：不传 assetRegistry / namedMaterialInstances（nullptr）——序列化层对这两个
// 走 graceful 退化（mesh / material 写出空字符串 + warn）。对 diff 而言这是**对称
// 无害**的：实例侧与模板侧用完全相同的退化路径，未被 override 的 mesh/material 在
// 两侧都写成同一空串，diff 不误报；真正被改的 mesh/material 各自反查 path 失败时
// 两侧都为空——这是已知局限（见汇报"摩擦"段），但不影响标量/数组字段的精确 diff。
std::string SerializeEntityComponents(const World& world, Entity entity)
{
    JsonWriter writer;
    if (!world.IsValid(entity))
    {
        return writer.Dump();  // "{}"
    }

    // 单 entity 持久 id 表：本 entity → 0。Hierarchy 等被排除，故互引用反查
    // 即使返回 -1 也无所谓；保留映射只为满足 SaveContext 契约。
    EntityToPersistentId idMap;
    idMap.emplace(entity, static_cast<std::int64_t>(0));

    const SaveContext ctx{world, idMap, /*assetRegistry=*/nullptr,
                          /*namedMaterialInstances=*/nullptr};

    const auto& serializers = GetBuiltinComponentSerializers();
    for (const auto& entry : serializers)
    {
        if (IsIdentityComponent(entry.name))
        {
            continue;
        }
        if (entry.Has != nullptr && entry.Has(world, entity))
        {
            const std::string componentPath = Join(kComponentsRoot, entry.name);
            if (entry.Write != nullptr)
            {
                entry.Write(writer, componentPath, entity, ctx);
            }
        }
    }
    return writer.Dump();
}

// 把某叶子节点的标量值序列化成一个可比较的规范字符串。类型探测顺序：bool →
// int → float → string。带类型前缀（'b'/'i'/'f'/'s'）避免跨类型误判相等
// （如 bool false 与 int 0、字符串 "1" 与 int 1）。任何类型都读不出（节点不存在 /
// 是空容器）→ 返回空串（前缀也无），表示"该路径无标量值"。
std::string LeafValueKey(const JsonReader& reader, std::string_view path)
{
    bool b = false;
    if (reader.ReadBool(path, b))
    {
        return std::string("b") + (b ? "1" : "0");
    }
    std::int64_t i = 0;
    if (reader.ReadInt(path, i))
    {
        return std::string("i") + std::to_string(i);
    }
    double d = 0.0;
    if (reader.ReadFloat(path, d))
    {
        // double → 文本：用足够精度的定点/科学表示，保证"相等的 double 得相同串、
        // 不同的得不同串"。ReadFloat 已含 int 路径（is_number 覆盖整型），但上面
        // ReadInt 先命中纯整数，这里只处理真正的浮点。
        return std::string("f") + std::to_string(d);
    }
    std::string s;
    if (reader.ReadString(path, s))
    {
        return std::string("s") + s;
    }
    return std::string{};  // 无标量值（不存在 / 空对象 / 空数组）
}

// 收集 reader 中 path 子树下所有"叶子路径"（相对 path 的后缀，'/' 分隔）。
// 对象 → 递归每个 key；数组 → 递归每个数字下标；否则（标量 / 空容器）→ path
// 本身是一个叶子，relativeOut 记一条（root 处 relative 为空串表示 component 根
// 即叶子，正常不会发生——component 都是对象）。
//
// relativePrefix 是相对 component 根累积的路径（不含 component 名那一段）。
void CollectLeafPaths(const JsonReader& reader,
                      const std::string& absolutePath,
                      const std::string& relativePrefix,
                      std::vector<std::string>& relativeOut)
{
    const std::vector<std::string> keys = reader.ListKeys(absolutePath);
    if (!keys.empty())
    {
        for (const std::string& key : keys)
        {
            const std::string childAbs = Join(absolutePath, key);
            const std::string childRel =
                relativePrefix.empty() ? key : Join(relativePrefix, key);
            CollectLeafPaths(reader, childAbs, childRel, relativeOut);
        }
        return;
    }

    const std::size_t arraySize = reader.ArraySize(absolutePath);
    if (arraySize > 0)
    {
        for (std::size_t idx = 0; idx < arraySize; ++idx)
        {
            const std::string idxStr = std::to_string(idx);
            const std::string childAbs = Join(absolutePath, idxStr);
            const std::string childRel =
                relativePrefix.empty() ? idxStr : Join(relativePrefix, idxStr);
            CollectLeafPaths(reader, childAbs, childRel, relativeOut);
        }
        return;
    }

    // 标量 / 空容器：本路径是一个叶子。
    relativeOut.push_back(relativePrefix);
}

// diff 单个 component：枚举实例侧 + 模板侧两份 JSON 中该 component 的全部叶子
// 路径（并集），逐叶子用 LeafValueKey 比较——值不同 / 单侧存在即一条 override。
void DiffComponent(const JsonReader& instReader,
                   const JsonReader& tmplReader,
                   const std::string& componentName,
                   std::vector<OverrideField>& out)
{
    const std::string instAbs = Join(kComponentsRoot, componentName);
    const std::string tmplAbs = Join(kComponentsRoot, componentName);

    // 两侧叶子路径并集（相对 component 根）。
    std::vector<std::string> leaves;
    CollectLeafPaths(instReader, instAbs, std::string{}, leaves);
    CollectLeafPaths(tmplReader, tmplAbs, std::string{}, leaves);
    std::sort(leaves.begin(), leaves.end());
    leaves.erase(std::unique(leaves.begin(), leaves.end()), leaves.end());

    for (const std::string& rel : leaves)
    {
        const std::string instLeafPath =
            rel.empty() ? instAbs : Join(instAbs, rel);
        const std::string tmplLeafPath =
            rel.empty() ? tmplAbs : Join(tmplAbs, rel);
        const std::string instKey = LeafValueKey(instReader, instLeafPath);
        const std::string tmplKey = LeafValueKey(tmplReader, tmplLeafPath);
        if (instKey != tmplKey)
        {
            out.push_back(OverrideField{componentName, rel});
        }
    }
}

// 比较两份 entity component JSON 文本，产出 override 字段集。
std::vector<OverrideField> DiffComponentJson(const std::string& instJson,
                                             const std::string& tmplJson)
{
    std::vector<OverrideField> result;

    auto instReaderRes = JsonReader::FromString(instJson);
    auto tmplReaderRes = JsonReader::FromString(tmplJson);
    if (instReaderRes.IsErr() || tmplReaderRes.IsErr())
    {
        // 自家 JsonWriter::Dump 的输出理应永远可解析；解析失败属内部错误，
        // 返回空集合（保守，不崩）。
        return result;
    }
    const JsonReader& instReader = instReaderRes.Value();
    const JsonReader& tmplReader = tmplReaderRes.Value();

    // 两侧 present component 名并集（component 级）。
    std::vector<std::string> componentNames;
    {
        const std::vector<std::string> instNames = instReader.ListKeys(kComponentsRoot);
        const std::vector<std::string> tmplNames = tmplReader.ListKeys(kComponentsRoot);
        componentNames.insert(componentNames.end(), instNames.begin(), instNames.end());
        componentNames.insert(componentNames.end(), tmplNames.begin(), tmplNames.end());
        std::sort(componentNames.begin(), componentNames.end());
        componentNames.erase(std::unique(componentNames.begin(), componentNames.end()),
                             componentNames.end());
    }

    for (const std::string& name : componentNames)
    {
        DiffComponent(instReader, tmplReader, name, result);
    }

    // 稳定排序：component 名优先，再叶子路径。便于测试断言与上层稳定消费。
    std::sort(result.begin(), result.end(),
              [](const OverrideField& a, const OverrideField& b)
              {
                  if (a.componentName != b.componentName)
                  {
                      return a.componentName < b.componentName;
                  }
                  return a.fieldPath < b.fieldPath;
              });
    return result;
}

}  // namespace

std::vector<OverrideField> ComputeEntityOverrides(
    const World& instWorld, Entity instEntity,
    const World& tmplWorld, Entity tmplEntity)
{
    if (!instWorld.IsValid(instEntity) || !tmplWorld.IsValid(tmplEntity))
    {
        return {};
    }
    const std::string instJson = SerializeEntityComponents(instWorld, instEntity);
    const std::string tmplJson = SerializeEntityComponents(tmplWorld, tmplEntity);
    return DiffComponentJson(instJson, tmplJson);
}

std::vector<OverrideField> ComputeInstanceOverrides(
    const World& instWorld, Entity instEntity, const Asset::PrefabAsset& tmpl)
{
    if (!instWorld.IsValid(instEntity))
    {
        return {};
    }

    // 取实例实体的模板锚（A2.2）。无 PrefabInstanceComponent / templateEntityGuid
    // 为空（旧数据）→ 无从配对，返回空。
    const auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instEntity);
    if (link == nullptr || !link->templateEntityGuid.IsValid())
    {
        return {};
    }

    // 模板 blob → scratch world。LoadFromString 不清空（这里 world 全新即空）。
    // 不传 assetRegistry——与 SerializeEntityComponents 的对称退化一致（mesh/material
    // 留空 handle，序列化时两侧都写空串）。
    World scratchWorld;
    auto loadRc = Scene::LoadFromString(tmpl.TemplateBlob(), scratchWorld);
    if (loadRc.IsErr())
    {
        return {};
    }

    // 经 guid 在模板 world 里反查对应模板实体（S2 / FindEntityByGuid）。未命中
    //（guid 在模板里不存在 / 坏数据）→ 返回空，不崩。
    const Entity tmplEntity =
        Scene::FindEntityByGuid(scratchWorld, link->templateEntityGuid);
    if (!tmplEntity.IsValid())
    {
        return {};
    }

    return ComputeEntityOverrides(instWorld, instEntity, scratchWorld, tmplEntity);
}

}  // namespace Orange::Engine::Scene
