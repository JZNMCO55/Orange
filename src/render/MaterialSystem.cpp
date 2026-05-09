// MaterialSystem 实现：unordered_map<string, Material> + 持有 AssetRegistry
// 引用。RegisterTemplate 走 registry.Load<ShaderAsset> 加载 SPIR-V → 装
// 配 Material 入表；FindTemplate 与 CreateInstance 按 name 查表。
//
// PIMPL 把 unordered_map / std::string 的复合类型藏在 .cpp，公共头继续
// 不漏 std::unordered_map。

#include "orange/engine/render/MaterialSystem.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/core/Log.h"
#include "orange/engine/render/BuiltinMaterials.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace Orange::Engine::Render
{

struct MaterialSystem::Impl
{
    explicit Impl(Asset::AssetRegistry& reg) noexcept
        : registry(reg)
    {
    }

    Asset::AssetRegistry&                  registry;
    std::unordered_map<std::string, Material> templates;
};

MaterialSystem::MaterialSystem(Asset::AssetRegistry& registry)
    : mpImpl(std::make_unique<Impl>(registry))
{
}

MaterialSystem::~MaterialSystem() = default;

MaterialSystem::MaterialSystem(MaterialSystem&&) noexcept            = default;
MaterialSystem& MaterialSystem::operator=(MaterialSystem&&) noexcept = default;

Result<void, ResultCode> MaterialSystem::RegisterTemplate(const ShaderTemplateDesc& desc)
{
    if (mpImpl->templates.find(desc.name) != mpImpl->templates.end())
    {
        return ResultCode::AlreadyExists;
    }

    // 把 desc 翻译成 Material 描述符。uniform / texture 槽布局直接拷贝；
    // shader handle 通过 registry.Load<ShaderAsset> 拿到——失败则保留无
    // 效 handle，与 BuiltinMaterials::LoadToon 失败语义一致。
    Material mat;
    mat.name          = desc.name;
    mat.uniforms      = desc.uniforms;
    mat.textureSlots  = desc.textureSlots;

    bool loadFailed = false;

    auto vsResult = mpImpl->registry.Load<Asset::ShaderAsset>(desc.vertexSpirvPath.string());
    if (vsResult.IsErr())
    {
        ORANGE_LOG_ERROR("MaterialSystem: 加载 vertex SPIR-V 失败 (template={}, path={}, code={})",
                         desc.name,
                         desc.vertexSpirvPath.string(),
                         static_cast<unsigned>(vsResult.Error()));
        loadFailed = true;
    }
    else
    {
        mat.vertexShader = vsResult.Value();
    }

    auto fsResult = mpImpl->registry.Load<Asset::ShaderAsset>(desc.fragmentSpirvPath.string());
    if (fsResult.IsErr())
    {
        ORANGE_LOG_ERROR("MaterialSystem: 加载 fragment SPIR-V 失败 (template={}, path={}, code={})",
                         desc.name,
                         desc.fragmentSpirvPath.string(),
                         static_cast<unsigned>(fsResult.Error()));
        loadFailed = true;
    }
    else
    {
        mat.fragmentShader = fsResult.Value();
    }

    // 不论是否半残都落进表——调用方查到 template 后可按 vertexShader.IsValid()
    // 自行判定。表的稳定地址承诺不能因加载失败丢失。
    mpImpl->templates.emplace(desc.name, std::move(mat));

    if (loadFailed)
    {
        return ResultCode::IoError;
    }
    return {};
}

const Material* MaterialSystem::FindTemplate(std::string_view name) const noexcept
{
    // unordered_map<string, ...>::find 接受 string_view 需要 C++20 transparent
    // 比较器；当前用临时 std::string 构造做查找——简单可移植，CreateInstance
    // / FindTemplate 不在每帧热路径上，开销可忽略。
    auto it = mpImpl->templates.find(std::string(name));
    if (it == mpImpl->templates.end())
    {
        return nullptr;
    }
    return &it->second;
}

std::unique_ptr<MaterialInstance> MaterialSystem::CreateInstance(std::string_view name)
{
    const Material* mat = FindTemplate(name);
    if (mat == nullptr)
    {
        return nullptr;
    }
    return std::make_unique<MaterialInstance>(mat);
}

std::size_t MaterialSystem::TemplateCount() const noexcept
{
    return mpImpl->templates.size();
}

Result<void, ResultCode> MaterialSystem::RegisterBuiltins()
{
    // 直接复用 BuiltinMaterials 的工厂——它们已经做完 .exe-相对路径解析
    // + AssetRegistry::Load<ShaderAsset>，返回填好的 Material 描述符。这
    // 里把整个 Material 直接落进 templates 表（不走 RegisterTemplate
    // 路径再装一次），避免 SPIR-V 加载重复 + 路径解析重复。
    //
    // 注：BuiltinMaterials 失败时也会返回带无效 shader handle 的 Material，
    // 这里照样存进表（与 RegisterTemplate 半残语义一致），但返回 IoError
    // 让调用方知情。
    auto storeIfNew = [this](Material mat) -> bool
    {
        if (mpImpl->templates.find(mat.name) != mpImpl->templates.end())
        {
            return false;
        }
        mpImpl->templates.emplace(mat.name, std::move(mat));
        return true;
    };

    Material textured = BuiltinMaterials::LoadTextured(mpImpl->registry);
    Material toon     = BuiltinMaterials::LoadToon(mpImpl->registry);
    Material rim      = BuiltinMaterials::LoadRimLight(mpImpl->registry);
    Material dissolve = BuiltinMaterials::LoadDissolve(mpImpl->registry);
    Material emissive = BuiltinMaterials::LoadEmissive(mpImpl->registry);

    const bool texturedShaderOk =
        textured.vertexShader.IsValid() && textured.fragmentShader.IsValid();
    const bool toonShaderOk = toon.vertexShader.IsValid() && toon.fragmentShader.IsValid();
    const bool rimShaderOk  = rim.vertexShader.IsValid()  && rim.fragmentShader.IsValid();
    const bool dissolveShaderOk =
        dissolve.vertexShader.IsValid() && dissolve.fragmentShader.IsValid();
    const bool emissiveShaderOk =
        emissive.vertexShader.IsValid() && emissive.fragmentShader.IsValid();

    const bool texturedNew = storeIfNew(std::move(textured));
    const bool toonNew     = storeIfNew(std::move(toon));
    const bool rimNew      = storeIfNew(std::move(rim));
    const bool dissolveNew = storeIfNew(std::move(dissolve));
    const bool emissiveNew = storeIfNew(std::move(emissive));

    if (!texturedNew || !toonNew || !rimNew || !dissolveNew || !emissiveNew)
    {
        return ResultCode::AlreadyExists;
    }
    if (!texturedShaderOk || !toonShaderOk || !rimShaderOk
        || !dissolveShaderOk || !emissiveShaderOk)
    {
        return ResultCode::IoError;
    }
    return {};
}

}  // namespace Orange::Engine::Render
