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
#include "orange/engine/core/Serialization.h"
#include "orange/engine/render/BuiltinMaterials.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace Orange::Engine::Render
{
namespace
{

// .exe 同目录解析 helper。与 BuiltinMaterials.cpp / Pipeline.cpp 重复——
// 三处后按 BuiltinMaterials.cpp 自述注释提到 Platform 模块统一，T1 阶段
// 先并存（"Three similar lines is better than a premature abstraction"
// 已过门槛，但抽出属独立整骨，不混入本 milestone）。
std::filesystem::path GetExecutableDir()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
    {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

// 把 .template.json 内的 SPV 路径字符串解析为绝对路径。绝对路径原样返
// 回；相对路径以 GetExecutableDir 为基（与 BuiltinMaterials::Load* 同款
// 约定，让 "shaders/orange_engine/<name>.spv" 指向 CMake 编译产物）。
std::filesystem::path ResolveSpvPath(std::string_view spvField)
{
    std::filesystem::path p(spvField);
    if (p.is_absolute())
    {
        return p;
    }
    return GetExecutableDir() / p;
}

// JSON "type" 字段字符串 → MaterialUniformType 枚举。未知字符串返回
// nullopt（调用方按缺省值 Float 处理 + log 警告）。
std::optional<MaterialUniformType> ParseUniformType(std::string_view s)
{
    if (s == "float") { return MaterialUniformType::Float; }
    if (s == "vec2")  { return MaterialUniformType::Vec2;  }
    if (s == "vec3")  { return MaterialUniformType::Vec3;  }
    if (s == "vec4")  { return MaterialUniformType::Vec4;  }
    if (s == "int")   { return MaterialUniformType::Int;   }
    if (s == "mat4")  { return MaterialUniformType::Mat4;  }
    return std::nullopt;
}

// 把一个 .template.json 文件解析为 ShaderTemplateDesc。失败返回 nullopt
// + log 已记录具体原因。
std::optional<ShaderTemplateDesc> LoadTemplateDescFromFile(
    const std::filesystem::path& jsonPath)
{
    auto readerResult = JsonReader::FromFile(jsonPath.string());
    if (readerResult.IsErr())
    {
        ORANGE_LOG_ERROR("MaterialSystem: 解析 .template.json 失败 (path={}, msg={})",
                         jsonPath.string(),
                         readerResult.Error().message);
        return std::nullopt;
    }
    const JsonReader& reader = readerResult.Value();

    // schemaVersion 校验：namespace 必须是 render/shader_template；major
    // 必须 == 1（T1 阶段单一主版本，未来 break 走 v2 + migrator）。
    auto verResult = reader.ReadSchemaVersion("schemaVersion");
    if (verResult.IsErr())
    {
        ORANGE_LOG_ERROR("MaterialSystem: .template.json schemaVersion 缺失或错误 (path={})",
                         jsonPath.string());
        return std::nullopt;
    }
    const SchemaVersion ver = verResult.Value();
    if (ver.Namespace() != "render/shader_template" || ver.Major() != 1)
    {
        ORANGE_LOG_ERROR("MaterialSystem: .template.json schemaVersion 不兼容 "
                         "(path={}, ns={}, ver={}.{})",
                         jsonPath.string(),
                         ver.Namespace(),
                         ver.Major(), ver.Minor());
        return std::nullopt;
    }

    ShaderTemplateDesc desc;
    if (!reader.ReadString("templateName", desc.name) || desc.name.empty())
    {
        ORANGE_LOG_ERROR("MaterialSystem: .template.json templateName 缺失或空 (path={})",
                         jsonPath.string());
        return std::nullopt;
    }

    std::string vertSpv;
    std::string fragSpv;
    if (!reader.ReadString("vertexSpv", vertSpv) || vertSpv.empty()
        || !reader.ReadString("fragmentSpv", fragSpv) || fragSpv.empty())
    {
        ORANGE_LOG_ERROR("MaterialSystem: .template.json vertexSpv / fragmentSpv 缺失 "
                         "(path={}, templateName={})",
                         jsonPath.string(), desc.name);
        return std::nullopt;
    }
    desc.vertexSpirvPath   = ResolveSpvPath(vertSpv);
    desc.fragmentSpirvPath = ResolveSpvPath(fragSpv);

    // uniforms 数组遍历。
    const std::size_t uniformCount = reader.ArraySize("uniforms");
    desc.uniforms.reserve(uniformCount);
    for (std::size_t i = 0; i < uniformCount; ++i)
    {
        const std::string base = "uniforms/" + std::to_string(i);
        std::string uName;
        std::string uType;
        if (!reader.ReadString(base + "/name", uName)
            || !reader.ReadString(base + "/type", uType))
        {
            ORANGE_LOG_ERROR("MaterialSystem: .template.json uniforms[{}] name/type 缺失 "
                             "(path={}, templateName={})",
                             i, jsonPath.string(), desc.name);
            continue;
        }
        const auto typeOpt = ParseUniformType(uType);
        if (!typeOpt.has_value())
        {
            ORANGE_LOG_ERROR("MaterialSystem: .template.json uniforms[{}] type 未知 "
                             "(path={}, name={}, type={})",
                             i, jsonPath.string(), uName, uType);
            continue;
        }
        MaterialUniformDesc ud;
        ud.name = std::move(uName);
        ud.type = *typeOpt;
        desc.uniforms.push_back(std::move(ud));
    }

    // textureSlots 数组遍历。
    const std::size_t slotCount = reader.ArraySize("textureSlots");
    desc.textureSlots.reserve(slotCount);
    for (std::size_t i = 0; i < slotCount; ++i)
    {
        const std::string base = "textureSlots/" + std::to_string(i);
        std::int64_t binding = 0;
        std::string  slotName;
        if (!reader.ReadInt(base + "/binding", binding)
            || !reader.ReadString(base + "/name", slotName))
        {
            ORANGE_LOG_ERROR("MaterialSystem: .template.json textureSlots[{}] "
                             "binding/name 缺失 (path={}, templateName={})",
                             i, jsonPath.string(), desc.name);
            continue;
        }
        MaterialTextureSlotDesc sd;
        sd.binding = static_cast<std::uint32_t>(binding);
        sd.name    = std::move(slotName);
        desc.textureSlots.push_back(std::move(sd));
    }

    return desc;
}

}  // namespace

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

std::vector<std::string> MaterialSystem::GetTemplateNames() const
{
    std::vector<std::string> names;
    names.reserve(mpImpl->templates.size());
    for (const auto& [name, _mat] : mpImpl->templates)
    {
        names.push_back(name);
    }
    return names;
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
    Material pbr      = BuiltinMaterials::LoadPbr(mpImpl->registry);

    const bool texturedShaderOk =
        textured.vertexShader.IsValid() && textured.fragmentShader.IsValid();
    const bool toonShaderOk = toon.vertexShader.IsValid() && toon.fragmentShader.IsValid();
    const bool rimShaderOk  = rim.vertexShader.IsValid()  && rim.fragmentShader.IsValid();
    const bool dissolveShaderOk =
        dissolve.vertexShader.IsValid() && dissolve.fragmentShader.IsValid();
    const bool emissiveShaderOk =
        emissive.vertexShader.IsValid() && emissive.fragmentShader.IsValid();
    const bool pbrShaderOk = pbr.vertexShader.IsValid() && pbr.fragmentShader.IsValid();

    const bool texturedNew = storeIfNew(std::move(textured));
    const bool toonNew     = storeIfNew(std::move(toon));
    const bool rimNew      = storeIfNew(std::move(rim));
    const bool dissolveNew = storeIfNew(std::move(dissolve));
    const bool emissiveNew = storeIfNew(std::move(emissive));
    const bool pbrNew      = storeIfNew(std::move(pbr));

    if (!texturedNew || !toonNew || !rimNew || !dissolveNew || !emissiveNew || !pbrNew)
    {
        return ResultCode::AlreadyExists;
    }
    if (!texturedShaderOk || !toonShaderOk || !rimShaderOk
        || !dissolveShaderOk || !emissiveShaderOk || !pbrShaderOk)
    {
        return ResultCode::IoError;
    }
    return {};
}

Result<void, ResultCode> MaterialSystem::RegisterTemplatesFromDirectory(
    const std::filesystem::path& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        ORANGE_LOG_WARN("MaterialSystem::RegisterTemplatesFromDirectory: 目录不存在 "
                        "或非目录 (dir={})", dir.string());
        return ResultCode::NotFound;
    }

    // 收集 *.template.json 候选文件（按字典序确保跨平台稳定的注册顺序）。
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file(ec)) { continue; }
        const fs::path& p = entry.path();
        const std::string fn = p.filename().string();
        // 后缀 ".template.json" 长度 14；按文件名后缀严格匹配，避免误吃
        // "foo.template.json.bak" 等历史 backup。
        constexpr std::string_view kSuffix = ".template.json";
        if (fn.size() < kSuffix.size()) { continue; }
        if (fn.compare(fn.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
        {
            continue;
        }
        candidates.push_back(p);
    }
    std::sort(candidates.begin(), candidates.end());

    if (candidates.empty())
    {
        ORANGE_LOG_WARN("MaterialSystem::RegisterTemplatesFromDirectory: 目录内无 "
                        "*.template.json (dir={})", dir.string());
        return {};
    }

    // 解析 + 注册。单文件失败仅 log 后跳过，不中断整目录扫描——与编辑器
    // 启动期的"有 template 能用就启动"语义一致。
    bool anyFailed = false;
    for (const auto& jsonPath : candidates)
    {
        auto descOpt = LoadTemplateDescFromFile(jsonPath);
        if (!descOpt.has_value())
        {
            anyFailed = true;
            continue;
        }
        auto regResult = RegisterTemplate(*descOpt);
        if (regResult.IsErr())
        {
            ORANGE_LOG_ERROR("MaterialSystem::RegisterTemplatesFromDirectory: "
                             "RegisterTemplate 失败 (file={}, templateName={}, code={})",
                             jsonPath.string(),
                             descOpt->name,
                             static_cast<unsigned>(regResult.Error()));
            anyFailed = true;
        }
    }

    return anyFailed ? Result<void, ResultCode>(ResultCode::IoError)
                     : Result<void, ResultCode>{};
}

}  // namespace Orange::Engine::Render
