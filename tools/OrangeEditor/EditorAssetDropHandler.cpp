// EditorAssetDropHandler 实现 —— 按 path 扩展名分派到 material / mesh /
// sound 三条 apply 路径，复用 EditorRenderLayer "Pick to Renderable.material"
// 同款 lambda + SetFieldValueCommand 命令栈模式。

#include "EditorAssetDropHandler.h"

#include "BuiltinAssets.h"
#include "EditorHost.h"
#include "command/EntityCommands.h"     // CreateEntityCommand（拖 mesh 到空白处建实体）
#include "command/SetFieldValueCommand.h"
#include "import/MetaSidecar.h"  // ReadTextureMeta（mesh .meta 的 subMeshMaterials 段）

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Orange::Editor
{
namespace
{

std::string GetExtension(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) { return {}; }
    return path.substr(dot);
}

bool IsMaterialExt(std::string_view ext) noexcept
{
    return ext == ".material";
}

bool IsMeshExt(std::string_view ext) noexcept
{
    return ext == ".mesh" || ext == ".obj";
}

bool IsAudioExt(std::string_view ext) noexcept
{
    return ext == ".wav" || ext == ".ogg"
        || ext == ".mp3" || ext == ".flac";
}

bool ApplyMaterial(EditorHost&                 host,
                   Orange::Engine::Entity      target,
                   const std::string&          path)
{
    using ::Orange::Engine::Render::RenderableComponent;

    if (host.scene.pWorld == nullptr
        || !host.scene.pWorld->IsValid(target))
    {
        ORANGE_LOG_WARN("ApplyAssetDropToEntity: entity invalid, skip "
                        "material '{}'", path);
        return false;
    }
    auto* rc = host.scene.pWorld->GetComponent<RenderableComponent>(target);
    if (rc == nullptr)
    {
        ORANGE_LOG_WARN("ApplyAssetDropToEntity: entity has no Renderable, "
                        "skip material '{}'", path);
        return false;
    }

    std::string oldPath;
    const auto named = BuildNamedMaterialInstances(host.assets);
    if (rc->materialInstance != nullptr)
    {
        for (const auto& [p, ptr] : named)
        {
            if (ptr == rc->materialInstance) { oldPath = p; break; }
        }
    }
    // v1.2.4 patch · apply lambda 走 EnsureMaterialInstance（含 lazy
    // create 兜底）—— 修复 v1.2.3 验收 bug：拖未被 Inspector 选过的新
    // .material 时 BuildNamedMaterialInstances 找不到 → 设 nullptr →
    // 物体材质显示 None。EnsureMaterialInstance 内 lazy CreateInstance
    // + ApplyDataToInstance + own 到 userMaterials 兜底。
    auto apply = [pH = &host, capE = target](const std::string& p) {
        auto* pW = pH->scene.pWorld.get();
        if (pW == nullptr || !pW->IsValid(capE)) { return; }
        auto* pRC = pW->GetComponent<RenderableComponent>(capE);
        if (pRC == nullptr) { return; }
        if (p.empty()) { pRC->materialInstance = nullptr; return; }
        pRC->materialInstance = EnsureMaterialInstance(*pH, p);
    };
    host.cmdStack.Push(
        std::make_unique<SetFieldValueCommand<std::string>>(
            target, "Renderable.materialInstance",
            oldPath, path, std::move(apply)));
    return true;
}

bool ApplyMesh(EditorHost&                 host,
               Orange::Engine::Entity      target,
               const std::string&          path)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Render::RenderableComponent;

    if (host.scene.pWorld == nullptr
        || !host.scene.pWorld->IsValid(target))
    {
        ORANGE_LOG_WARN("ApplyAssetDropToEntity: entity invalid, skip "
                        "mesh '{}'", path);
        return false;
    }
    auto* rc = host.scene.pWorld->GetComponent<RenderableComponent>(target);
    if (rc == nullptr)
    {
        ORANGE_LOG_WARN("ApplyAssetDropToEntity: entity has no Renderable, "
                        "skip mesh '{}'", path);
        return false;
    }

    std::string oldPath;
    if (rc->mesh.IsValid() && host.assets.pAssets != nullptr)
    {
        oldPath = std::string{
            host.assets.pAssets->PathOf<MeshAsset>(rc->mesh)};
    }
    auto apply = [pH = &host, capE = target](const std::string& p) {
        auto* pW = pH->scene.pWorld.get();
        if (pW == nullptr || !pW->IsValid(capE)) { return; }
        auto* pRC = pW->GetComponent<RenderableComponent>(capE);
        if (pRC == nullptr) { return; }
        if (p.empty())
        {
            pRC->mesh = {};
            // 空 path → 撤掉多材质组件（避免 stale slot 指向旧 mesh 的分段）。
            SyncSubMeshMaterialsForMesh(*pH, capE, p);
            return;
        }
        auto* pReg = pH->assets.pAssets.get();
        if (pReg == nullptr) { return; }
        auto lr = pReg->Load<MeshAsset>(p);
        if (lr.IsErr()) { return; }
        pRC->mesh = lr.Value();
        // 多材质 slot 同步：attach/replace/remove SubMeshMaterialsComponent +
        // slot 0 兜底 Renderable.materialInstance。与 Inspector 设 Renderable.mesh
        // 字段复用同一函数（SyncSubMeshMaterialsForMesh），保证两路径一致。
        SyncSubMeshMaterialsForMesh(*pH, capE, p);
    };
    host.cmdStack.Push(
        std::make_unique<SetFieldValueCommand<std::string>>(
            target, "Renderable.mesh",
            oldPath, path, std::move(apply)));
    return true;
}

bool ApplySound(EditorHost&                 host,
                Orange::Engine::Entity      target,
                const std::string&          path)
{
    using ::Orange::Engine::Asset::SoundAsset;
    using ::Orange::Engine::Audio::AudioSourceComponent;

    if (host.scene.pWorld == nullptr
        || !host.scene.pWorld->IsValid(target))
    {
        ORANGE_LOG_WARN("ApplyAssetDropToEntity: entity invalid, skip "
                        "sound '{}'", path);
        return false;
    }
    auto* ac = host.scene.pWorld->GetComponent<AudioSourceComponent>(target);
    if (ac == nullptr)
    {
        ORANGE_LOG_WARN("ApplyAssetDropToEntity: entity has no AudioSource, "
                        "skip sound '{}'", path);
        return false;
    }

    std::string oldPath;
    if (ac->sound.IsValid() && host.assets.pAssets != nullptr)
    {
        oldPath = std::string{
            host.assets.pAssets->PathOf<SoundAsset>(ac->sound)};
    }
    auto apply = [pH = &host, capE = target](const std::string& p) {
        auto* pW = pH->scene.pWorld.get();
        if (pW == nullptr || !pW->IsValid(capE)) { return; }
        auto* pAS = pW->GetComponent<AudioSourceComponent>(capE);
        if (pAS == nullptr) { return; }
        if (p.empty()) { pAS->sound = {}; return; }
        auto* pReg = pH->assets.pAssets.get();
        if (pReg == nullptr) { return; }
        auto lr = pReg->Load<SoundAsset>(p);
        if (lr.IsOk()) { pAS->sound = lr.Value(); }
    };
    host.cmdStack.Push(
        std::make_unique<SetFieldValueCommand<std::string>>(
            target, "AudioSource.sound",
            oldPath, path, std::move(apply)));
    return true;
}

}  // namespace

void SyncSubMeshMaterialsForMesh(EditorHost&            host,
                                 Orange::Engine::Entity entity,
                                 const std::string&     meshPath)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Render::MaterialInstance;
    using ::Orange::Engine::Render::RenderableComponent;
    using ::Orange::Engine::Render::SubMeshMaterialsComponent;

    auto* pW = host.scene.pWorld.get();
    if (pW == nullptr || !pW->IsValid(entity)) { return; }
    auto* pRC = pW->GetComponent<RenderableComponent>(entity);
    if (pRC == nullptr) { return; }

    auto removeIfPresent = [&]() {
        if (pW->HasComponent<SubMeshMaterialsComponent>(entity))
        {
            pW->RemoveComponent<SubMeshMaterialsComponent>(entity);
        }
    };

    // 空 path（清 mesh）→ 撤掉残留组件，避免 stale slot 指向旧 mesh 分段。
    if (meshPath.empty()) { removeIfPresent(); return; }

    auto* pReg = host.assets.pAssets.get();
    if (pReg == nullptr) { return; }
    auto lr = pReg->Load<MeshAsset>(meshPath);
    if (lr.IsErr()) { return; }

    // 回读同名 .meta 的 subMeshMaterials 段（导入侧对单 + 多 material 都写）。
    // 无映射（纯几何 / 手工 .mesh / 旧产物）→ 不动材质，仅撤掉可能残留的旧组件。
    const std::string metaPath = ::Orange::Editor::Import::MetaPathFor(meshPath);
    const auto metaOpt = ::Orange::Editor::Import::ReadTextureMeta(metaPath);
    if (!metaOpt.has_value() || metaOpt->subMeshMaterials.empty())
    {
        removeIfPresent();
        return;
    }

    // 按 slot 顺序经 EnsureMaterialInstance（lazy create + own 到 userMaterials,
    // 与 Material drop 同款 owner 机制）解析 .material → MaterialInstance*。
    std::vector<MaterialInstance*> resolved;
    resolved.reserve(metaOpt->subMeshMaterials.size());
    for (const std::string& matPath : metaOpt->subMeshMaterials)
    {
        // 空 slot（无 material 的 primitive 占位）→ nullptr，渲染端回退默认。
        resolved.push_back(matPath.empty()
                               ? nullptr
                               : ::EnsureMaterialInstance(host, matPath));
    }

    const MeshAsset* pMesh = pReg->Get<MeshAsset>(lr.Value());
    const bool meshHasSubMeshes = (pMesh != nullptr && pMesh->HasSubMeshes());

    if (meshHasSubMeshes && resolved.size() > 1)
    {
        // 多 material：挂 SubMeshMaterialsComponent（各 sub-mesh 段独立材质）+
        // slot 0 当 Renderable.materialInstance 兜底（与单 material 语义一致）。
        SubMeshMaterialsComponent smc;
        smc.slots = resolved;
        if (!smc.slots.empty() && smc.slots[0] != nullptr)
        {
            pRC->materialInstance = smc.slots[0];
        }
        if (pW->HasComponent<SubMeshMaterialsComponent>(entity))
        {
            *pW->GetComponent<SubMeshMaterialsComponent>(entity) = std::move(smc);
        }
        else
        {
            pW->AddComponent<SubMeshMaterialsComponent>(entity, std::move(smc));
        }
    }
    else
    {
        // 单 material（或单段多 material 退化）：直接设 Renderable.materialInstance
        // = 第一个非空材质 —— 单材质模型拖进场景即带导入的材质（对齐 Lumix /
        // Unity，不再 drop 后是默认材质）。不挂 SubMeshMaterialsComponent，撤掉
        // 可能残留的旧组件（切到单材质 mesh 时清掉前一个多材质 mesh 的分段）。
        for (MaterialInstance* inst : resolved)
        {
            if (inst != nullptr) { pRC->materialInstance = inst; break; }
        }
        removeIfPresent();
    }
}

bool ApplyAssetDropToEntity(EditorHost&                 host,
                            Orange::Engine::Entity      target,
                            const std::string&          assetPath)
{
    if (assetPath.empty() || !target.IsValid()) { return false; }
    const std::string ext = GetExtension(assetPath);
    if (IsMaterialExt(ext)) { return ApplyMaterial(host, target, assetPath); }
    if (IsMeshExt(ext))     { return ApplyMesh(host, target, assetPath); }
    if (IsAudioExt(ext))    { return ApplySound(host, target, assetPath); }
    ORANGE_LOG_WARN("ApplyAssetDropToEntity: 不识别的扩展名 (path={}, ext={})",
                    assetPath, ext);
    return false;
}

Orange::Engine::Entity
CreateEntityFromMeshAsset(EditorHost&        host,
                          const std::string& meshPath,
                          const glm::vec3&   position)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::World;
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Render::MaterialInstance;
    using ::Orange::Engine::Render::RenderableComponent;
    using ::Orange::Engine::Render::SubMeshMaterialsComponent;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;

    const std::string ext = GetExtension(meshPath);
    if (!IsMeshExt(ext))
    {
        ORANGE_LOG_WARN("CreateEntityFromMeshAsset: 非 mesh 扩展名 (path={}, ext={})",
                        meshPath, ext);
        return Entity::Invalid();
    }
    auto* pReg = host.assets.pAssets.get();
    auto* pW   = host.scene.pWorld.get();
    if (pReg == nullptr || pW == nullptr) { return Entity::Invalid(); }

    auto lr = pReg->Load<MeshAsset>(meshPath);
    if (lr.IsErr())
    {
        ORANGE_LOG_WARN("CreateEntityFromMeshAsset: mesh 加载失败 '{}'", meshPath);
        return Entity::Invalid();
    }
    const auto       meshHandle = lr.Value();
    const MeshAsset* pMesh      = pReg->Get<MeshAsset>(meshHandle);
    const bool       hasSubs    = (pMesh != nullptr && pMesh->HasSubMeshes());

    // 预解析材质（host 在手）：读 .meta subMeshMaterials → EnsureMaterialInstance。
    // 解析出的指针由 host.assets 持有（生命周期跟 host），capture 进 factory 后
    // undo/redo 重放仍有效。
    std::vector<MaterialInstance*> resolved;
    const std::string metaPath = ::Orange::Editor::Import::MetaPathFor(meshPath);
    const auto        metaOpt  = ::Orange::Editor::Import::ReadTextureMeta(metaPath);
    if (metaOpt.has_value())
    {
        for (const std::string& m : metaOpt->subMeshMaterials)
        {
            resolved.push_back(m.empty() ? nullptr
                                         : ::EnsureMaterialInstance(host, m));
        }
    }
    // 默认材质兜底（无 .meta 材质 / slot 0 空时让新物体仍可见）。
    MaterialInstance* defMat = host.assets.pPbrMaterial
        ? host.assets.pPbrMaterial.get()
        : host.assets.pDefaultRenderableMaterial.get();
    MaterialInstance* primary = defMat;
    for (MaterialInstance* m : resolved)
    {
        if (m != nullptr) { primary = m; break; }
    }
    const bool multi = hasSubs && resolved.size() > 1;

    // 实体名 = mesh 文件 stem。
    std::string name;
    {
        const auto slash = meshPath.find_last_of("/\\");
        const std::string file =
            (slash == std::string::npos) ? meshPath : meshPath.substr(slash + 1);
        const auto dot = file.find_last_of('.');
        name = (dot == std::string::npos) ? file : file.substr(0, dot);
        if (name.empty()) { name = "Mesh"; }
    }

    auto cmd = std::make_unique<CreateEntityCommand>(
        host,
        [meshHandle, primary, resolved, multi, position, name](World& w) -> Entity
        {
            Entity e = w.CreateEntity();
            w.AddComponent<NameComponent>(e, NameComponent{name});
            TransformComponent tc{};
            tc.position = position;
            w.AddComponent<TransformComponent>(e, tc);
            RenderableComponent rc{};
            rc.mesh             = meshHandle;
            rc.materialInstance = primary;
            rc.visible          = true;
            rc.castsShadow      = true;
            w.AddComponent<RenderableComponent>(e, rc);
            if (multi)
            {
                SubMeshMaterialsComponent smc;
                smc.slots = resolved;
                w.AddComponent<SubMeshMaterialsComponent>(e, std::move(smc));
            }
            return e;
        });
    auto* raw = cmd.get();
    host.cmdStack.Push(std::move(cmd));
    return raw->CreatedEntity();
}

}  // namespace Orange::Editor
