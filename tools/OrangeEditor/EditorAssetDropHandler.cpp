// EditorAssetDropHandler 实现 —— 按 path 扩展名分派到 material / mesh /
// sound 三条 apply 路径，复用 EditorRenderLayer "Pick to Renderable.material"
// 同款 lambda + SetFieldValueCommand 命令栈模式。

#include "EditorAssetDropHandler.h"

#include "BuiltinAssets.h"
#include "EditorHost.h"
#include "command/SetFieldValueCommand.h"

#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/audio/AudioSourceComponent.h>
#include <orange/engine/core/Log.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/World.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
    auto apply = [pH = &host, capE = target](const std::string& p) {
        auto* pW = pH->scene.pWorld.get();
        if (pW == nullptr || !pW->IsValid(capE)) { return; }
        auto* pRC = pW->GetComponent<RenderableComponent>(capE);
        if (pRC == nullptr) { return; }
        if (p.empty()) { pRC->materialInstance = nullptr; return; }
        const auto m = BuildNamedMaterialInstances(pH->assets);
        auto it = m.find(p);
        pRC->materialInstance = (it != m.end()) ? it->second : nullptr;
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
        if (p.empty()) { pRC->mesh = {}; return; }
        auto* pReg = pH->assets.pAssets.get();
        if (pReg == nullptr) { return; }
        auto lr = pReg->Load<MeshAsset>(p);
        if (lr.IsOk()) { pRC->mesh = lr.Value(); }
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
