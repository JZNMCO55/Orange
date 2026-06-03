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

// 把 entity 的非身份 component（排除身份/链接 component）经已注册的内置
// ComponentSerializer 写进既有 writer（挂在 kComponentsRoot 下）。复用 SaveImpl 的
// 逐 entity 序列化循环：建一个只含本 entity（持久 id = 0）的最小 SaveContext，
// 遍历注册表，对 Has 命中的 component 调其 Write。entity 无效 → writer 不变。
// 返回实际写出的 component 名（CS2 的 merge / write-back 据此枚举要回写哪些 component）。
//
// 这是 SerializeEntityComponents 与 CS2 共享的核心：CS1 取其 Dump() 文本做 diff；
// CS2 既取 dump 文本（diff + 配对 reader），又直接拿 writer（在其上就地覆盖 override
// 叶子——数组叶子已是正确 JSON array，覆盖 array 元素需要 writer 里数组已成形）。
//
// 注意：不传 assetRegistry / namedMaterialInstances（nullptr）——序列化层对这两个
// 走 graceful 退化（mesh / material 写出空字符串 + warn）。对 diff 而言这是**对称
// 无害**的：实例侧与模板侧用完全相同的退化路径，未被 override 的 mesh/material 在
// 两侧都写成同一空串，diff 不误报；真正被改的 mesh/material 各自反查 path 失败时
// 两侧都为空——这是已知局限（见汇报"摩擦"段），但不影响标量/数组字段的精确 diff。
// CS2 写回侧同理：未改 mesh/material 在 refresh 后两侧仍是空串，asset-ref 不被
// 重设（与 CS1 对称），对未改资产引用无害。
std::vector<std::string> SerializeEntityComponentsToWriter(const World& world,
                                                           Entity entity,
                                                           JsonWriter& writer)
{
    std::vector<std::string> writtenNames;
    if (!world.IsValid(entity))
    {
        return writtenNames;
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
                writtenNames.emplace_back(entry.name);
            }
        }
    }
    return writtenNames;
}

// 把单个 entity 的 present component（排除身份/链接 component）经已注册的内置
// ComponentSerializer 写成 JSON 文本，挂在 kComponentsRoot 下。
//
// 返回 JsonWriter::Dump() 的 JSON 文本。entity 无效 → 返回空对象文本（"{}"），
// diff 时与"无任何 component"等价。
std::string SerializeEntityComponents(const World& world, Entity entity)
{
    JsonWriter writer;
    SerializeEntityComponentsToWriter(world, entity, writer);
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

// ---------------------------------------------------------------------------
// CS2 · refresh-from-template 的 merge + write-back。
// ---------------------------------------------------------------------------

// 把 srcReader 在 path 处的单个叶子标量值，按类型探测（bool→int→float→string，
// 同 LeafValueKey 顺序）拷进 dstWriter 的同一 path。已存在叶子被覆盖；当目标父节
// 点已是 JSON array 且本段是数字下标时，EnsureByPath 走数组分支就地改元素（故覆
// 盖 override 的数组叶子要求 dstWriter 里数组已成形——由先 replay 模板 Write 保证）。
// 源处无标量值（不存在 / 空容器）→ 不写（保留 dst 原值）。
void CopyLeafValue(const JsonReader& srcReader,
                   std::string_view path,
                   JsonWriter& dstWriter)
{
    bool b = false;
    if (srcReader.ReadBool(path, b))
    {
        dstWriter.WriteBool(path, b);
        return;
    }
    std::int64_t i = 0;
    if (srcReader.ReadInt(path, i))
    {
        dstWriter.WriteInt(path, i);
        return;
    }
    double d = 0.0;
    if (srcReader.ReadFloat(path, d))
    {
        dstWriter.WriteFloat(path, d);
        return;
    }
    std::string s;
    if (srcReader.ReadString(path, s))
    {
        dstWriter.WriteString(path, s);
        return;
    }
    // 源无标量值：override 叶子在实例侧消失（罕见，本期固定 schema 下不发生）→
    // 保留 dst（模板值）不动。
}

// 产出 merged component JSON 文本 M = 新模板（theirs）为底、真 override 叶子用实例
// 值就地覆盖。
//
//   newWriter  —— 已 replay 过新模板（theirs）entity 的非身份 component Write（含正确
//                 数组形态），在其上就地覆盖真 override 叶子（直接 mutate，调用方不再
//                 复用它）。
//   instReader —— 实例（mine）entity 的 component JSON（真 override 叶子的实例值来源）。
//   overrides  —— CS1 diff(mine, base) 出的真 override 字段集（componentName + 相对
//                 component 根的叶子路径）。逐条把实例值贴回 newWriter 对应绝对路径。
//
// 返回 newWriter.Dump()——未 override 叶子=新模板值（replay 时已写）、override 叶子=
// 实例值（本步覆盖）。身份 component 从一开始就不在 newWriter 里，故 M 不含它们。
std::string BuildMergedComponentJson(JsonWriter& newWriter,
                                     const JsonReader& instReader,
                                     const std::vector<OverrideField>& overrides)
{
    for (const OverrideField& field : overrides)
    {
        const std::string componentAbs = Join(kComponentsRoot, field.componentName);
        const std::string leafAbs =
            field.fieldPath.empty() ? componentAbs : Join(componentAbs, field.fieldPath);
        CopyLeafValue(instReader, leafAbs, newWriter);
    }
    return newWriter.Dump();
}

// 把 merged JSON M 的各 component 经已注册 ComponentSerializer 的 Read 反序列化
// **写回 instEntity**（AddComponent 走 emplace_or_replace，整段替换该 component）。
//
// componentNames = M 里要回写的 component 名（即模板 entity 写出过的非身份 component）。
// 仅这些被回写——身份 component 不在 M 里、也不在本表里，故绝不触碰。
//
// LoadContext 只填能填的：world = 实例 world；idToEntity 把持久 id 0 → instEntity
// （非身份 component 自包含、不含跨实体引用，这张表实际不会被用到，仅满足契约）；
// 其余 backend / registry 指针留空——非身份 component（Transform/Name/Renderable…）
// 的 Read 对这些走 graceful 退化（与序列化侧对称）。
//
// 任一 component 的 Read 返回 false（数据坏）→ 记 false 但继续其余 component（部分
// 刷新好过整盘拒绝；正常路径 M 来自自家 Write 不会坏）。整体成功 → true。
bool WriteBackMergedComponents(World& instWorld,
                               Entity instEntity,
                               const std::string& mergedJson,
                               const std::vector<std::string>& componentNames)
{
    auto readerRes = JsonReader::FromString(mergedJson);
    if (readerRes.IsErr())
    {
        return false;  // 自家 Dump 理应永远可解析；解析失败属内部错误。
    }
    const JsonReader& reader = readerRes.Value();

    // 持久 id 0 → instEntity（满足 LoadContext 契约；非身份 component 不解引用它）。
    PersistentIdToEntity idToEntity;
    idToEntity.push_back(instEntity);

    const LoadContext ctx{instWorld, idToEntity, /*assetRegistry=*/nullptr,
                          /*physicsWorld=*/nullptr, /*animatorRegistry=*/nullptr};

    const auto& serializers = GetBuiltinComponentSerializers();
    bool allOk = true;
    for (const std::string& name : componentNames)
    {
        // 在注册表里找同名 entry 的 Read（PureData 组件才有 Read；身份 component
        // 已被排除在 componentNames 之外）。
        const ComponentSerializerEntry* entry = nullptr;
        for (const auto& e : serializers)
        {
            if (e.name == name)
            {
                entry = &e;
                break;
            }
        }
        if (entry == nullptr || entry->Read == nullptr)
        {
            // backend-dependent 组件（RigidBody/Collider/Animator）Read 为 nullptr：
            // 它们的 attach 需 backend，本期 refresh 不重建 backend → 跳过（保留实例
            // 现有该 component 不动，不视为失败）。
            continue;
        }
        const std::string componentPath = Join(kComponentsRoot, name);
        if (!entry->Read(reader, componentPath, instEntity, ctx))
        {
            allOk = false;  // 继续其余 component（部分刷新好过整盘拒绝）。
        }
    }
    return allOk;
}

// 把 overriddenPaths 里的扁平串 "componentName/fieldPath" 拆回 OverrideField
// （componentName = 第一段，fieldPath = 余下全部，可含 '/'）。无 '/' → fieldPath 空
// （整 component 级 override，本期罕见但合法）。空串跳过（返回 false）。
bool SplitOverridePath(std::string_view flat, OverrideField& out)
{
    if (flat.empty())
    {
        return false;
    }
    const std::size_t slash = flat.find('/');
    if (slash == std::string_view::npos)
    {
        out.componentName.assign(flat);
        out.fieldPath.clear();
        return true;
    }
    out.componentName.assign(flat.substr(0, slash));
    out.fieldPath.assign(flat.substr(slash + 1));
    return true;
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

bool RefreshEntityFromTemplate(World& instWorld, Entity instEntity,
                               const World& baseWorld, Entity baseEntity,
                               const World& newWorld, Entity newEntity)
{
    if (!instWorld.IsValid(instEntity) || !baseWorld.IsValid(baseEntity)
        || !newWorld.IsValid(newEntity))
    {
        return false;
    }

    // ① theirs（新模板）entity 的非身份 component → writer（含正确数组形态），
    //    同时拿到模板写出过的 component 名（write-back 的回写集）。无任何非身份
    //    component → 没什么可刷的，merged 即空对象，写回 no-op，成功返回 true。
    JsonWriter mergedWriter;
    const std::vector<std::string> componentNames =
        SerializeEntityComponentsToWriter(newWorld, newEntity, mergedWriter);

    // ② mine（实例）+ base（bake 时模板）→ JSON。
    const std::string instJson = SerializeEntityComponents(instWorld, instEntity);
    const std::string baseJson = SerializeEntityComponents(baseWorld, baseEntity);
    auto instReaderRes = JsonReader::FromString(instJson);
    if (instReaderRes.IsErr())
    {
        return false;  // 自家 Dump 理应可解析。
    }
    const JsonReader& instReader = instReaderRes.Value();

    // ③ CS1 diff(mine, base)：实例相对 bake 时模板的**真 override** 叶子集
    //    （区别于"实例 vs 新模板"——后者把模板演进也误当 override，见头注释）。
    const std::vector<OverrideField> overrides = DiffComponentJson(instJson, baseJson);

    // ④ merged M = 新模板为底、真 override 叶子贴回实例值。
    const std::string mergedJson =
        BuildMergedComponentJson(mergedWriter, instReader, overrides);

    // ⑤ 把 M 的各 component Read 写回实例实体（emplace_or_replace 整段替换）。
    return WriteBackMergedComponents(instWorld, instEntity, mergedJson, componentNames);
}

bool RefreshInstanceFromTemplate(World& instWorld, Entity instEntity,
                                 const Asset::PrefabAsset& baseTmpl,
                                 const Asset::PrefabAsset& newTmpl)
{
    if (!instWorld.IsValid(instEntity))
    {
        return false;
    }

    // 取实例实体的模板锚（A2.2）。无 PrefabInstanceComponent / templateEntityGuid
    // 为空（旧数据）→ 无从配对，no-op。
    const auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instEntity);
    if (link == nullptr || !link->templateEntityGuid.IsValid())
    {
        return false;
    }

    // base / new 两个模板 blob → 各自 scratch world（同 ComputeInstanceOverrides
    // 的配对逻辑）。不传 assetRegistry——与序列化对称退化一致。
    World baseWorld;
    World newWorld;
    if (Scene::LoadFromString(baseTmpl.TemplateBlob(), baseWorld).IsErr()
        || Scene::LoadFromString(newTmpl.TemplateBlob(), newWorld).IsErr())
    {
        return false;
    }

    // 经 templateEntityGuid 在两个模板 world 里各自反查对应模板实体（S2 /
    // FindEntityByGuid）。base 锚定的是同一 guid（实例 bake 时与演进后模板共享
    // per-entity guid——模板演进改字段值不换 guid）。任一未命中 → no-op，不崩。
    const Entity baseEntity =
        Scene::FindEntityByGuid(baseWorld, link->templateEntityGuid);
    const Entity newEntity =
        Scene::FindEntityByGuid(newWorld, link->templateEntityGuid);
    if (!baseEntity.IsValid() || !newEntity.IsValid())
    {
        return false;
    }

    return RefreshEntityFromTemplate(instWorld, instEntity,
                                     baseWorld, baseEntity, newWorld, newEntity);
}

bool RefreshInstanceFromTemplate(World& instWorld, Entity instEntity,
                                 const Asset::PrefabAsset& tmpl)
{
    // 单模板便利重载：base == new（未演进）→ 退化为"丢弃实例的非真 override 漂移、
    // 拉回模板值"。委托三方重载，两个模板参数同传。
    return RefreshInstanceFromTemplate(instWorld, instEntity, tmpl, tmpl);
}

// ---------------------------------------------------------------------------
// 持久化 overriddenPaths 的记录 / 查询 / 移除（C1 / ADR-019 问题 4）。
// ---------------------------------------------------------------------------

std::string MakeOverridePath(std::string_view componentName, std::string_view fieldPath)
{
    if (fieldPath.empty())
    {
        return std::string(componentName);
    }
    std::string out;
    out.reserve(componentName.size() + 1 + fieldPath.size());
    out.append(componentName);
    out.push_back('/');
    out.append(fieldPath);
    return out;
}

bool RecordOverridePath(PrefabInstanceComponent& link,
                        std::string_view componentName,
                        std::string_view fieldPath)
{
    const std::string path = MakeOverridePath(componentName, fieldPath);
    // dedup：已存在则不重复加。
    if (std::find(link.overriddenPaths.begin(), link.overriddenPaths.end(), path)
        != link.overriddenPaths.end())
    {
        return false;
    }
    link.overriddenPaths.push_back(path);
    return true;
}

bool IsPathOverridden(const PrefabInstanceComponent& link,
                      std::string_view componentName,
                      std::string_view fieldPath)
{
    const std::string path = MakeOverridePath(componentName, fieldPath);
    return std::find(link.overriddenPaths.begin(), link.overriddenPaths.end(), path)
           != link.overriddenPaths.end();
}

bool ClearOverridePath(PrefabInstanceComponent& link,
                       std::string_view componentName,
                       std::string_view fieldPath)
{
    const std::string path = MakeOverridePath(componentName, fieldPath);
    const auto it =
        std::find(link.overriddenPaths.begin(), link.overriddenPaths.end(), path);
    if (it == link.overriddenPaths.end())
    {
        return false;
    }
    link.overriddenPaths.erase(it);
    return true;
}

// ---------------------------------------------------------------------------
// RefreshInstanceWithRecordedOverrides —— 用持久化 overriddenPaths 当显式 override 集
// 做 refresh（C1 / ADR-019 问题 4）。复用 CS2 的 merge 机器，只把 override 叶子集来源
// 换成实例的 overriddenPaths（不再需要 bake 时 base 快照）。
// ---------------------------------------------------------------------------

bool RefreshInstanceWithRecordedOverrides(World& instWorld, Entity instEntity,
                                          const Asset::PrefabAsset& tmpl)
{
    if (!instWorld.IsValid(instEntity))
    {
        return false;
    }

    // 取实例实体的模板锚 + 显式 override 集（A2.2 + ADR-019）。无
    // PrefabInstanceComponent / templateEntityGuid 为空（旧数据）→ 无从配对，no-op。
    const auto* link = instWorld.GetComponent<PrefabInstanceComponent>(instEntity);
    if (link == nullptr || !link->templateEntityGuid.IsValid())
    {
        return false;
    }

    // 模板 blob → scratch world（同 ComputeInstanceOverrides 的配对逻辑）。
    // 不传 assetRegistry——与序列化对称退化一致（mesh/material 留空 handle）。
    World tmplWorld;
    if (Scene::LoadFromString(tmpl.TemplateBlob(), tmplWorld).IsErr())
    {
        return false;
    }

    // 经 templateEntityGuid 在模板 world 反查对应模板实体（S2 / FindEntityByGuid）。
    // 未命中（guid 在模板里不存在 / 坏数据）→ no-op，不崩。
    const Entity tmplEntity =
        Scene::FindEntityByGuid(tmplWorld, link->templateEntityGuid);
    if (!tmplEntity.IsValid())
    {
        return false;
    }

    // 把持久化 overriddenPaths 扁平串拆回 OverrideField 集（CS2 merge 的输入形态）。
    // 这正是与 CS2 三方 merge 的区别点：override 集来源是"持久化的显式记录"而非
    // "CS1 diff(mine, base)"——故无需 base 模板快照。
    std::vector<OverrideField> overrides;
    overrides.reserve(link->overriddenPaths.size());
    for (const std::string& flat : link->overriddenPaths)
    {
        OverrideField field;
        if (SplitOverridePath(flat, field))
        {
            overrides.push_back(std::move(field));
        }
    }

    // ① theirs（模板）entity 的非身份 component → writer（含正确数组形态）+ 回写集。
    JsonWriter mergedWriter;
    const std::vector<std::string> componentNames =
        SerializeEntityComponentsToWriter(tmplWorld, tmplEntity, mergedWriter);

    // ② mine（实例）entity 的 component JSON（override 叶子的实例值来源）。
    const std::string instJson = SerializeEntityComponents(instWorld, instEntity);
    auto instReaderRes = JsonReader::FromString(instJson);
    if (instReaderRes.IsErr())
    {
        return false;  // 自家 Dump 理应可解析。
    }
    const JsonReader& instReader = instReaderRes.Value();

    // ③ merged M = 模板为底、overriddenPaths 指定的叶子用实例值就地覆盖。
    const std::string mergedJson =
        BuildMergedComponentJson(mergedWriter, instReader, overrides);

    // ④ 把 M 的各 component Read 写回实例实体（emplace_or_replace 整段替换）。
    return WriteBackMergedComponents(instWorld, instEntity, mergedJson, componentNames);
}

}  // namespace Orange::Engine::Scene
