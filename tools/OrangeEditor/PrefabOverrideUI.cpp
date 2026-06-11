#include "PrefabOverrideUI.h"

#include "EditorHost.h"
#include "render/ThumbnailService.h"  // Invalidate（apply 重写 .prefab 后失效旧缩略图）

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/asset/PrefabLoader.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/scene/PrefabInstanceComponent.h>
#include <orange/engine/scene/PrefabOverride.h>
#include <orange/engine/scene/World.h>

#include <algorithm>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// override path 粒度（本 TU 的核心约束）
//
// 引擎层 override path = 序列化叶子粒度 "componentName/fieldPath"，fieldPath 是
// JSON 叶子路径：标量字段直接是字段名（"intensity"），Vec/Quat 是数组下标叶子
// （Transform 的 "position/0" / "position/1" / "position/2"）。编辑器 schema 字段
// 是整字段粒度（prop.name = "position"）。
//
// 为弥合二者且不手拼/猜叶子名，本 TU 不自己生成 override path——而是用引擎层
// 的 Scene::ComputeInstanceOverrides（CS1 diff）算出**权威叶子集**：
//   * 记录（SyncRecordedOverrides）= 把 overriddenPaths 同步成这个权威叶子集
//     → 与 revert / refresh 消费的叶子粒度严格一致。
//   * 蓝条（IsFieldOverridden）= "componentName/propName" 作为叶子路径**前缀**
//     匹配（"position" 命中 "position/0".."position/2"；标量 "intensity" 精确相等）。
//
// nested prop（schema prop.name 形如 "desc.emissionRate"）：序列化叶子用 '/'
// 分隔（"desc/emissionRate"），故前缀匹配前把 prop.name 的 '.' 统一替换成 '/'。
//
// 蓝条 / 记录用 ComputeInstanceOverrides（运行时 diff），不依赖 overriddenPaths
// 也能精确显示 override；overriddenPaths 的持久化由 SyncRecordedOverrides 写回，
// 作"重开场景仍显示蓝条 + 喂给 refresh 的显式 override 集"。
//
// apply / refresh 不进命令栈
//
// ApplyInstanceToPrefab 改写磁盘上的 .prefab.json（资产层动作），与 world mutate
// 不同——无法用命令栈安全回滚（撤销要恢复磁盘文件旧内容 + 已 reload 的 handle）。
// 与 EditorPrefabActions::CommitNewPrefabFile「创建资产文件是磁盘动作不进命令栈」
// 同口径。RefreshInstanceWithRecordedOverrides 是 world mutate，但它一次重写实例
// 多个 component 的多字段、缺 typed in-place 逆写原语做精确 Undo——本期同样作
// 显式用户动作落地、不进命令栈（与 apply 对齐；见报告"需主循环拍板"）。单字段
// revert 走命令栈可 Undo，实现在 SchemaInspector 字段右键（typed old/new 值，
// 复用 SetFieldValueCommand）——本 TU 不承载 revert。
// ---------------------------------------------------------------------------

namespace Orange::Editor::Prefab
{

namespace
{

namespace EScene = Orange::Engine::Scene;
using Orange::Engine::Entity;
using Orange::Engine::World;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::PrefabAsset;

// 当帧 override 叶子集缓存（BeginFrameForEntity 填，IsFieldOverridden /
// SyncRecordedOverrides 读）。module-level static：Inspector 单线程绘制、同一
// 时刻只在看一个实体的字段，无并发。非 prefab 实例 → sCacheValid=false。
Entity                             sCacheEntity = Entity::Invalid();
bool                               sCacheValid  = false;
std::vector<EScene::OverrideField> sCacheOverrides;

// prop.name 的 '.' → '/'（nested prop 与序列化叶子路径对齐）。
std::string NormalizePropPath(std::string_view propName)
{
    std::string out(propName);
    std::replace(out.begin(), out.end(), '.', '/');
    return out;
}

// fieldPath 是否落在 "propPath" 前缀下：相等（标量）或以 "propPath/" 开头
// （Vec/Quat 子叶子）。
bool LeafUnderProp(const std::string& fieldPath, const std::string& propPath)
{
    if (fieldPath == propPath) { return true; }
    return fieldPath.size() > propPath.size()
        && fieldPath.compare(0, propPath.size(), propPath) == 0
        && fieldPath[propPath.size()] == '/';
}

}  // anonymous namespace

bool IsPrefabInstance(EditorHost& host, Entity entity)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return false; }
    const auto* link = pWorld->GetComponent<EScene::PrefabInstanceComponent>(entity);
    return link != nullptr && link->templateEntityGuid.IsValid();
}

const PrefabAsset* ResolveTemplate(EditorHost& host, Entity entity)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return nullptr; }
    const auto* link = pWorld->GetComponent<EScene::PrefabInstanceComponent>(entity);
    if (link == nullptr || link->sourcePrefabPath.empty()) { return nullptr; }

    auto* pReg = host.assets.pAssets.get();
    if (pReg == nullptr) { return nullptr; }
    auto loaded = pReg->Load<PrefabAsset>(link->sourcePrefabPath);
    if (loaded.IsErr()) { return nullptr; }
    return pReg->Get<PrefabAsset>(loaded.Value());
}

void BeginFrameForEntity(EditorHost& host, Entity entity)
{
    sCacheEntity = entity;
    sCacheValid  = false;
    sCacheOverrides.clear();

    if (!IsPrefabInstance(host, entity)) { return; }

    const PrefabAsset* tmpl = ResolveTemplate(host, entity);
    if (tmpl == nullptr) { return; }

    auto* pWorld = host.scene.pWorld.get();
    sCacheOverrides = EScene::ComputeInstanceOverrides(*pWorld, entity, *tmpl);
    sCacheValid     = true;
}

bool IsFieldOverridden(std::string_view componentName, std::string_view propName)
{
    if (!sCacheValid) { return false; }
    const std::string propPath = NormalizePropPath(propName);
    for (const auto& f : sCacheOverrides)
    {
        if (f.componentName == componentName && LeafUnderProp(f.fieldPath, propPath))
        {
            return true;
        }
    }
    return false;
}

bool SyncRecordedOverrides(EditorHost& host, Entity entity)
{
    // 强制重算当帧最新 diff —— 调用点在 DrawEntityViaSchemas 末尾（字段编辑命令
    // 已落地），帧首 BeginFrameForEntity 的缓存是"编辑前"的，这里必须重算才能
    // 捕获本帧改动。BeginFrameForEntity 同时刷新 module 缓存，供下一帧蓝条用。
    BeginFrameForEntity(host, entity);
    if (!sCacheValid) { return false; }

    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return false; }
    auto* link = pWorld->GetComponent<EScene::PrefabInstanceComponent>(entity);
    if (link == nullptr) { return false; }

    // 目标集 = 当前真实 diff 的扁平串集合（MakeOverridePath 规范化）。
    std::vector<std::string> desired;
    desired.reserve(sCacheOverrides.size());
    for (const auto& f : sCacheOverrides)
    {
        desired.push_back(EScene::MakeOverridePath(f.componentName, f.fieldPath));
    }
    std::sort(desired.begin(), desired.end());
    desired.erase(std::unique(desired.begin(), desired.end()), desired.end());

    // 与现有 overriddenPaths 比对：相等则 no-op（避免每帧标脏 / 改组件）。
    std::vector<std::string> current = link->overriddenPaths;
    std::sort(current.begin(), current.end());
    current.erase(std::unique(current.begin(), current.end()), current.end());
    if (current == desired) { return false; }

    link->overriddenPaths = std::move(desired);
    return true;
}

bool RevertField(EditorHost& host, Entity entity,
                 std::string_view componentName, std::string_view propName)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return false; }
    if (!IsPrefabInstance(host, entity)) { return false; }

    const PrefabAsset* tmpl = ResolveTemplate(host, entity);
    if (tmpl == nullptr) { return false; }

    // 该 prop 下被 override 的全部序列化叶子（标量 = 1 条；Vec/Quat = 多条）。
    // 用当帧最新 diff（调用方右键弹出前已 BeginFrameForEntity；缓存非本 entity
    // 则即时重算）。
    if (!sCacheValid || sCacheEntity != entity) { BeginFrameForEntity(host, entity); }
    const std::string propPath = NormalizePropPath(propName);
    std::vector<std::string> leaves;
    for (const auto& f : sCacheOverrides)
    {
        if (f.componentName == componentName && LeafUnderProp(f.fieldPath, propPath))
        {
            leaves.push_back(f.fieldPath);
        }
    }
    if (leaves.empty()) { return false; }  // 该字段无 override，无需 revert

    // 逐叶子 revert（引擎原语：以实例为底、仅该叶子贴模板值、回写该 component +
    // ClearOverridePath 清该 path 记录）。Vec/Quat 的多个分量叶子各自 revert。
    bool anyOk = false;
    for (const std::string& leaf : leaves)
    {
        if (EScene::RevertInstanceOverridePath(*pWorld, entity, *tmpl,
                                               componentName, leaf))
        {
            anyOk = true;
        }
    }

    // revert 改了字段值（含可能的 Transform.rotation）→ invalidate Euler 缓存，
    // 下一帧 Quat case 从最新 quat 重算 Euler 显示。
    host.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();

    // 重算缓存：蓝条立即去掉该字段（overriddenPaths 也已被引擎 Clear 该 path）。
    BeginFrameForEntity(host, entity);
    return anyOk;
}

bool RevertAllFields(EditorHost& host, Entity entity)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return false; }
    if (!IsPrefabInstance(host, entity)) { return false; }

    const PrefabAsset* tmpl = ResolveTemplate(host, entity);
    if (tmpl == nullptr) { return false; }

    // 当前真实 override 叶子集（强制重算，避免 stale 缓存）。
    BeginFrameForEntity(host, entity);
    if (!sCacheValid || sCacheOverrides.empty()) { return false; }

    // 逐叶子 revert（拷贝快照——RevertInstanceOverridePath 会改 overriddenPaths，
    // 不能边迭代边改 module 缓存指向的同一 vector）。
    const std::vector<EScene::OverrideField> snapshot = sCacheOverrides;
    bool anyOk = false;
    for (const auto& f : snapshot)
    {
        if (EScene::RevertInstanceOverridePath(*pWorld, entity, *tmpl,
                                               f.componentName, f.fieldPath))
        {
            anyOk = true;
        }
    }
    host.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    BeginFrameForEntity(host, entity);  // 重算缓存（蓝条应全清）
    return anyOk;
}

bool ApplyInstanceToPrefab(EditorHost& host, Entity entity)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return false; }
    if (!IsPrefabInstance(host, entity)) { return false; }

    const auto* link = pWorld->GetComponent<EScene::PrefabInstanceComponent>(entity);
    if (link == nullptr || link->sourcePrefabPath.empty()) { return false; }
    const std::string prefabPath = link->sourcePrefabPath;

    // 现有 prefabName（重写时保留人类可读名；取不到用路径兜底）。先取——
    // ApplyInstanceToTemplate 之后再 Load 会拿到新 blob，但 prefabName 不变。
    std::string prefabName = prefabPath;
    if (const PrefabAsset* t = ResolveTemplate(host, entity); t != nullptr)
    {
        prefabName = t->PrefabName();
    }

    // ① 实例当前态 → 新模板 blob（引擎原语；guid 映回 templateEntityGuid 保 A2.2
    //    锚不断；清本实例 overriddenPaths）。调用方应传实例**根**——内部从传入
    //    实体序列化子树为根。
    auto blobRes = EScene::ApplyInstanceToTemplate(*pWorld, entity);
    if (blobRes.IsErr())
    {
        ORANGE_LOG_ERROR("Apply to Prefab '{}' 失败 —— 重建模板 blob (code={})",
                         prefabPath, static_cast<unsigned>(blobRes.Error()));
        return false;
    }

    // ② 重写 .prefab.json（纯 IO；不进命令栈，见头注释）。
    auto saveRc = Orange::Engine::Asset::PrefabLoader::Save(
        prefabPath, prefabName, blobRes.Value());
    if (saveRc.IsErr())
    {
        ORANGE_LOG_ERROR("Apply to Prefab '{}' 写盘失败 (code={})",
                         prefabPath, static_cast<unsigned>(saveRc.Error()));
        return false;
    }

    // ③ 失效 AssetRegistry 里缓存的旧 PrefabAsset（Load 按 path dedup，不 Unload
    //    会一直返回旧 blob）。Unload 后下一次 ResolveTemplate 的 Load 重新从盘读
    //    新模板。失败缩略图同步失效。
    if (auto* pReg = host.assets.pAssets.get(); pReg != nullptr)
    {
        auto h = pReg->Load<PrefabAsset>(prefabPath);
        if (h.IsOk()) { pReg->Unload<PrefabAsset>(h.Value()); }
    }
    if (host.thumbnails)
    {
        host.thumbnails->Invalidate(prefabPath);
    }

    ORANGE_LOG_INFO("Apply to Prefab '{}' 成功（已重写模板 + 失效缓存）", prefabPath);
    return true;
}

bool RefreshInstanceFromPrefab(EditorHost& host, Entity entity)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || !pWorld->IsValid(entity)) { return false; }
    if (!IsPrefabInstance(host, entity)) { return false; }

    const PrefabAsset* tmpl = ResolveTemplate(host, entity);
    if (tmpl == nullptr) { return false; }

    // 用持久化 overriddenPaths 当显式 override 集，从模板重拉非 override 字段
    // （引擎原语）。不进命令栈（缺 typed in-place 逆写原语做精确多字段 Undo；
    // 与 apply 对齐，作显式用户动作）。改了 Transform 时 invalidate Euler 缓存。
    const bool ok =
        EScene::RefreshInstanceWithRecordedOverrides(*pWorld, entity, *tmpl);
    host.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
    if (ok)
    {
        // refresh 后重算缓存（蓝条立即反映新 override 集；overriddenPaths 不变）。
        BeginFrameForEntity(host, entity);
    }
    return ok;
}

}  // namespace Orange::Editor::Prefab
