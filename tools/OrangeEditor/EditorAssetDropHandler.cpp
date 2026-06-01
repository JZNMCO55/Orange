// EditorAssetDropHandler 实现 —— 按 path 扩展名分派到 material / mesh /
// sound 三条 apply 路径，复用 EditorRenderLayer "Pick to Renderable.material"
// 同款 lambda + SetFieldValueCommand 命令栈模式。

#include "EditorAssetDropHandler.h"

#include "BuiltinAssets.h"
#include "EditorHost.h"
#include "command/SetFieldValueCommand.h"
#include "import/MetaSidecar.h"  // ReadTextureMeta（mesh .meta 的 subMeshMaterials 段）

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/SubMeshMaterialsComponent.h>
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
        using ::Orange::Engine::Render::SubMeshMaterialsComponent;

        auto* pW = pH->scene.pWorld.get();
        if (pW == nullptr || !pW->IsValid(capE)) { return; }
        auto* pRC = pW->GetComponent<RenderableComponent>(capE);
        if (pRC == nullptr) { return; }
        if (p.empty())
        {
            pRC->mesh = {};
            // 清 mesh 时一并撤掉多材质组件（避免 stale slot 指向旧 mesh 的分段）。
            if (pW->HasComponent<SubMeshMaterialsComponent>(capE))
            {
                pW->RemoveComponent<SubMeshMaterialsComponent>(capE);
            }
            return;
        }
        auto* pReg = pH->assets.pAssets.get();
        if (pReg == nullptr) { return; }
        auto lr = pReg->Load<MeshAsset>(p);
        if (lr.IsErr()) { return; }
        pRC->mesh = lr.Value();

        // 多 material mesh（HasSubMeshes()）：回读同名 .meta 的 subMeshMaterials
        // 段，按 slot 顺序经 EnsureMaterialInstance（lazy create + own 到
        // userMaterials，与单 material drop 完全同款 owner 机制）挂
        // SubMeshMaterialsComponent；slot 0 同时设为 Renderable.materialInstance
        // 兜底。单 material mesh 不带 sub-mesh，跳过本段、撤掉可能残留的旧组件，
        // 行为与历史单材质 drop 完全一致。
        const MeshAsset* pMesh = pReg->Get<MeshAsset>(lr.Value());
        const bool meshHasSubMeshes = (pMesh != nullptr && pMesh->HasSubMeshes());
        if (!meshHasSubMeshes)
        {
            if (pW->HasComponent<SubMeshMaterialsComponent>(capE))
            {
                pW->RemoveComponent<SubMeshMaterialsComponent>(capE);
            }
            return;
        }

        const std::string metaPath = ::Orange::Editor::Import::MetaPathFor(p);
        const auto metaOpt = ::Orange::Editor::Import::ReadTextureMeta(metaPath);
        if (!metaOpt.has_value() || metaOpt->subMeshMaterials.empty())
        {
            // mesh 带 sub-mesh 但 .meta 无映射（手工 .mesh / 旧产物）—— 不挂组件，
            // 渲染端各段都回退到 Renderable.materialInstance（单 material 兜底）。
            return;
        }

        SubMeshMaterialsComponent smc;
        smc.slots.reserve(metaOpt->subMeshMaterials.size());
        for (const std::string& matPath : metaOpt->subMeshMaterials)
        {
            // 空 slot（无 material 的 primitive 占位）→ nullptr，渲染端回退默认。
            smc.slots.push_back(matPath.empty()
                                    ? nullptr
                                    : ::EnsureMaterialInstance(*pH, matPath));
        }
        // slot 0 当 Renderable.materialInstance 兜底（与单 material 语义一致）。
        if (!smc.slots.empty() && smc.slots[0] != nullptr)
        {
            pRC->materialInstance = smc.slots[0];
        }
        if (pW->HasComponent<SubMeshMaterialsComponent>(capE))
        {
            *pW->GetComponent<SubMeshMaterialsComponent>(capE) = std::move(smc);
        }
        else
        {
            pW->AddComponent<SubMeshMaterialsComponent>(capE, std::move(smc));
        }
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

}  // namespace Orange::Editor
