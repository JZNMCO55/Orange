// MaterialInstance 实现：把 SetUniform / SetTexture 的 per-instance 覆
// 盖落到两张 unordered_map 上。uniform 值用 type tag + 64-byte blob 表
// 达，避免 std::variant<glm 多类型> 渗到公共头。
//
// SetUniform 路径上做两件事：
//   1. 在 Material->uniforms 里按 name 查 type；
//   2. 若 type 与本重载预期匹配，把 64-byte blob 写入 override 表。
// 任一步失败（name 不在 / type 不匹配）都是 no-op——见
// MaterialInstance.h 的 silent-ignore 决策记录。

#include "orange/engine/render/MaterialInstance.h"

#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

namespace Orange::Engine::Render
{
namespace
{

// uniform value 存储：type 标签 + 64 字节定长 blob（mat4 = 16 floats =
// 64 字节，是当前最大宽度）。新增 uniform 类型若超过 64 字节，要在这
// 里同步扩容。
struct UniformValue
{
    MaterialUniformType type{MaterialUniformType::Float};
    std::array<std::byte, sizeof(glm::mat4)> bytes{};
};

template <typename T>
UniformValue MakeUniformValue(MaterialUniformType type, const T& value)
{
    static_assert(sizeof(T) <= sizeof(UniformValue::bytes),
                  "uniform 值超过 64-byte blob 容量；扩容 bytes 或重新审视存储策略");
    UniformValue v;
    v.type = type;
    std::memcpy(v.bytes.data(), &value, sizeof(T));
    return v;
}

// 在 Material 的 uniform 描述符里按 name 查找。返回 nullptr 表示不存在。
const MaterialUniformDesc* FindUniform(const Material* pMaterial, std::string_view name)
{
    if (pMaterial == nullptr)
    {
        return nullptr;
    }
    for (const auto& desc : pMaterial->uniforms)
    {
        if (desc.name == name)
        {
            return &desc;
        }
    }
    return nullptr;
}

bool HasTextureSlot(const Material* pMaterial, std::uint32_t binding)
{
    if (pMaterial == nullptr)
    {
        return false;
    }
    for (const auto& slot : pMaterial->textureSlots)
    {
        if (slot.binding == binding)
        {
            return true;
        }
    }
    return false;
}

}  // namespace

struct MaterialInstance::Impl
{
    const Material* pMaterial{nullptr};

    // 用 std::string 作为 key（heterogenous lookup 在 unordered_map 上
    // 要 C++20 transparent hash + key_equal，复杂度暂时不值得引入）。
    // SetUniform 的 std::string_view 在这里材化成
    // std::string——uniform 写入路径通常不在帧内热路径上。
    std::unordered_map<std::string, UniformValue>                                uniformOverrides;
    std::unordered_map<std::uint32_t, Asset::AssetHandle<Asset::TextureAsset>>   textureOverrides;
};

MaterialInstance::MaterialInstance(const Material* pMaterial)
    : mpImpl(std::make_unique<Impl>())
{
    mpImpl->pMaterial = pMaterial;
}

MaterialInstance::~MaterialInstance() = default;

MaterialInstance::MaterialInstance(MaterialInstance&&) noexcept            = default;
MaterialInstance& MaterialInstance::operator=(MaterialInstance&&) noexcept = default;

const Material* MaterialInstance::GetMaterial() const noexcept
{
    return mpImpl ? mpImpl->pMaterial : nullptr;
}

namespace
{

// SetUniform 主体：查 desc + 比对 type + 写 override。
template <typename T>
void StoreUniform(MaterialInstance::Impl& impl,
                  std::string_view        name,
                  MaterialUniformType     expectedType,
                  const T&                value)
{
    const MaterialUniformDesc* desc = FindUniform(impl.pMaterial, name);
    if (desc == nullptr || desc->type != expectedType)
    {
        return;  // no-op：name 不存在 或 type 不匹配
    }
    impl.uniformOverrides[std::string(name)] = MakeUniformValue(expectedType, value);
}

}  // namespace

void MaterialInstance::SetUniform(std::string_view name, float value)
{
    if (mpImpl) StoreUniform(*mpImpl, name, MaterialUniformType::Float, value);
}

void MaterialInstance::SetUniform(std::string_view name, std::int32_t value)
{
    if (mpImpl) StoreUniform(*mpImpl, name, MaterialUniformType::Int, value);
}

void MaterialInstance::SetUniform(std::string_view name, const glm::vec2& value)
{
    if (mpImpl) StoreUniform(*mpImpl, name, MaterialUniformType::Vec2, value);
}

void MaterialInstance::SetUniform(std::string_view name, const glm::vec3& value)
{
    if (mpImpl) StoreUniform(*mpImpl, name, MaterialUniformType::Vec3, value);
}

void MaterialInstance::SetUniform(std::string_view name, const glm::vec4& value)
{
    if (mpImpl) StoreUniform(*mpImpl, name, MaterialUniformType::Vec4, value);
}

void MaterialInstance::SetUniform(std::string_view name, const glm::mat4& value)
{
    if (mpImpl) StoreUniform(*mpImpl, name, MaterialUniformType::Mat4, value);
}

void MaterialInstance::SetTexture(std::uint32_t                            binding,
                                  Asset::AssetHandle<Asset::TextureAsset> handle)
{
    if (!mpImpl)
    {
        return;
    }
    if (!HasTextureSlot(mpImpl->pMaterial, binding))
    {
        return;  // no-op：binding 不在 Material 的 textureSlots 里
    }
    mpImpl->textureOverrides[binding] = handle;
}

bool MaterialInstance::HasUniformOverride(std::string_view name) const noexcept
{
    if (!mpImpl)
    {
        return false;
    }
    // unordered_map<std::string, ...> 不支持 string_view 直接查；先材
    // 化一份临时 std::string。query 不在帧内热路径，可以接受。
    return mpImpl->uniformOverrides.find(std::string(name)) != mpImpl->uniformOverrides.end();
}

bool MaterialInstance::HasTextureOverride(std::uint32_t binding) const noexcept
{
    if (!mpImpl)
    {
        return false;
    }
    return mpImpl->textureOverrides.find(binding) != mpImpl->textureOverrides.end();
}

namespace
{

// 通用读回：按 name 查 uniformOverrides；命中且 type 与请求一致 → 拷贝
// blob 到 T；否则返回 nullopt。SetUniform 已经在写入路径上保证 type 与
// blob 内容匹配，这里读回直接 memcpy 回 T。
template <typename T>
std::optional<T> ReadUniform(const MaterialInstance::Impl* pImpl,
                             std::string_view              name,
                             MaterialUniformType           expectedType) noexcept
{
    if (pImpl == nullptr)
    {
        return std::nullopt;
    }
    auto it = pImpl->uniformOverrides.find(std::string(name));
    if (it == pImpl->uniformOverrides.end() || it->second.type != expectedType)
    {
        return std::nullopt;
    }
    T value{};
    static_assert(sizeof(T) <= sizeof(UniformValue::bytes),
                  "读回类型超过 64-byte blob 容量");
    std::memcpy(&value, it->second.bytes.data(), sizeof(T));
    return value;
}

}  // namespace

std::optional<float> MaterialInstance::GetUniformFloat(std::string_view name) const noexcept
{
    return ReadUniform<float>(mpImpl.get(), name, MaterialUniformType::Float);
}

std::optional<std::int32_t> MaterialInstance::GetUniformInt(std::string_view name) const noexcept
{
    return ReadUniform<std::int32_t>(mpImpl.get(), name, MaterialUniformType::Int);
}

std::optional<glm::vec2> MaterialInstance::GetUniformVec2(std::string_view name) const noexcept
{
    return ReadUniform<glm::vec2>(mpImpl.get(), name, MaterialUniformType::Vec2);
}

std::optional<glm::vec3> MaterialInstance::GetUniformVec3(std::string_view name) const noexcept
{
    return ReadUniform<glm::vec3>(mpImpl.get(), name, MaterialUniformType::Vec3);
}

std::optional<glm::vec4> MaterialInstance::GetUniformVec4(std::string_view name) const noexcept
{
    return ReadUniform<glm::vec4>(mpImpl.get(), name, MaterialUniformType::Vec4);
}

std::optional<glm::mat4> MaterialInstance::GetUniformMat4(std::string_view name) const noexcept
{
    return ReadUniform<glm::mat4>(mpImpl.get(), name, MaterialUniformType::Mat4);
}

Asset::AssetHandle<Asset::TextureAsset>
MaterialInstance::GetTextureBinding(std::uint32_t binding) const noexcept
{
    if (!mpImpl)
    {
        return {};
    }
    auto it = mpImpl->textureOverrides.find(binding);
    if (it == mpImpl->textureOverrides.end())
    {
        return {};
    }
    return it->second;
}

std::vector<std::string> MaterialInstance::GetUniformOverrideNames() const
{
    std::vector<std::string> names;
    if (!mpImpl)
    {
        return names;
    }
    names.reserve(mpImpl->uniformOverrides.size());
    for (const auto& [name, _val] : mpImpl->uniformOverrides)
    {
        names.push_back(name);
    }
    return names;
}

std::vector<std::uint32_t> MaterialInstance::GetTextureOverrideBindings() const
{
    std::vector<std::uint32_t> bindings;
    if (!mpImpl)
    {
        return bindings;
    }
    bindings.reserve(mpImpl->textureOverrides.size());
    for (const auto& [binding, _handle] : mpImpl->textureOverrides)
    {
        bindings.push_back(binding);
    }
    return bindings;
}

std::optional<MaterialUniformType>
MaterialInstance::GetUniformOverrideType(std::string_view name) const noexcept
{
    if (!mpImpl)
    {
        return std::nullopt;
    }
    auto it = mpImpl->uniformOverrides.find(std::string(name));
    if (it == mpImpl->uniformOverrides.end())
    {
        return std::nullopt;
    }
    return it->second.type;
}

}  // namespace Orange::Engine::Render
