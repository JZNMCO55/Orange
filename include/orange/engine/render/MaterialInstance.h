#ifndef ORANGE_ENGINE_RENDER_MATERIAL_INSTANCE_H
#define ORANGE_ENGINE_RENDER_MATERIAL_INSTANCE_H

// ---------------------------------------------------------------------------
// MaterialInstance —— Material 模板的单实例覆盖。
//
// 每个 MaterialInstance 绑定一个 const Material*（caller-owned），并维
// 护一份 per-instance 的 uniform / texture 覆盖。Pipeline 在每帧渲染时
// 先从 Material 取默认值，再用 Instance 的覆盖
// 增量补上——典型用法：同一 Material 模板被多个 entity 共享，每个 entity
// 通过自己的 MaterialInstance 覆盖个别 uniform（颜色 / 时间 / 纹理）。
//
// SetUniform / SetTexture 的 name 与 binding 必须在 Material 的描述符
// 列表里出现且类型匹配；否则调用是 no-op（不抛、不崩、不 log——0.x
// 阶段优先吞 schema 演化造成的"旧调用方喂新 template"路径）。如果发现
// silent-ignore 隐藏了真 bug，再考虑加可选 ORANGE_LOG_WARN。
//
// PIMPL：override 存储用 unordered_map + type-tagged blob，藏在 .cpp，
// 公共头不暴露 std::variant<glm 多类型> 这种"传染性"的复合 std 类型。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialTypes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Render
{

class ORANGE_ENGINE_API MaterialInstance
{
public:
    // pMaterial == nullptr 是受支持的退化状态（"还没 bind template 的
    // 半构造态"）；所有 SetXxx 在这种状态下都是 no-op。它和
    // MaterialSystem::CreateInstance 配套，正常路径下不会出现
    // null instance。
    explicit MaterialInstance(const Material* pMaterial = nullptr);
    ~MaterialInstance();

    MaterialInstance(const MaterialInstance&)            = delete;
    MaterialInstance& operator=(const MaterialInstance&) = delete;

    MaterialInstance(MaterialInstance&&) noexcept;
    MaterialInstance& operator=(MaterialInstance&&) noexcept;

    const Material* GetMaterial() const noexcept;

    // SetUniform —— 按 name 在 GetMaterial()->uniforms 里查找；
    //   * 不存在 → no-op；
    //   * 存在但 type 与本重载不匹配 → no-op；
    //   * 否则把值存进 per-instance override 表，覆盖 Material 的默认值。
    void SetUniform(std::string_view name, float value);
    void SetUniform(std::string_view name, std::int32_t value);
    void SetUniform(std::string_view name, const glm::vec2& value);
    void SetUniform(std::string_view name, const glm::vec3& value);
    void SetUniform(std::string_view name, const glm::vec4& value);
    void SetUniform(std::string_view name, const glm::mat4& value);

    // SetTexture —— 按 binding 在 GetMaterial()->textureSlots 里查找；
    // 不存在 → no-op。binding 命中即记录此 slot 的覆盖 handle（可以是
    // 无效 handle，表示"显式清除"）。
    void SetTexture(std::uint32_t binding,
                    Asset::AssetHandle<Asset::TextureAsset> handle);

    // 存在性查询，用于单元测试 / Pipeline 走 fast-path 跳过未覆盖的
    // uniform 槽。
    bool HasUniformOverride(std::string_view name) const noexcept;
    bool HasTextureOverride(std::uint32_t binding) const noexcept;

    // 读回 API —— Pipeline 在按 MaterialInstance 路由 push-constant /
    // 描述符时按 Material.uniforms 列表逐项 GetUniformXxx；返回的
    // optional 表示是否有 per-instance 覆盖（无覆盖时调用方应回退到
    // Material 的默认值——0.x 阶段 Material 不存默认值，调用方按 zero
    // 处理）。type 与签名不匹配时同样返回 nullopt。
    std::optional<float>          GetUniformFloat(std::string_view name) const noexcept;
    std::optional<std::int32_t>   GetUniformInt(std::string_view name) const noexcept;
    std::optional<glm::vec2>      GetUniformVec2(std::string_view name) const noexcept;
    std::optional<glm::vec3>      GetUniformVec3(std::string_view name) const noexcept;
    std::optional<glm::vec4>      GetUniformVec4(std::string_view name) const noexcept;
    std::optional<glm::mat4>      GetUniformMat4(std::string_view name) const noexcept;

    // 取得 binding 对应槽位的覆盖纹理 handle。无覆盖时返回 default-init
    // 的无效 handle（IsValid() == false）；调用方按 IsValid() 判定。
    Asset::AssetHandle<Asset::TextureAsset>
        GetTextureBinding(std::uint32_t binding) const noexcept;

    // 枚举所有已设置 uniform override 的 name —— 让序列化 / Inspector
    // 调参 UI 等"不知道 name 的情况下遍历"路径可行。返回值类型 vector
    // 拷贝，免后续 SetUniform rehash 让 view 悬挂。
    //
    // 顺序未定义：内部 unordered_map 遍历无序。调用方需要稳定输出顺序
    // （如序列化写盘）时自行 sort。
    //
    // 不在帧内热路径——典型消费方是 Save / Inspector 列举，与
    // SetUniform / GetUniformXxx 同节奏；不强调零分配。
    std::vector<std::string> GetUniformOverrideNames() const;

    // 枚举所有已设置 texture override 的 binding。同上语义。
    std::vector<std::uint32_t> GetTextureOverrideBindings() const;

    // 取得某 uniform override 的实际类型。命中返回 type；未命中返回
    // nullopt。Save 路径用 type 决定写哪个 GetUniformXxx + 写盘的 type
    // 字段；Inspector 用 type 决定显示哪种调参控件。
    std::optional<MaterialUniformType>
        GetUniformOverrideType(std::string_view name) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_MATERIAL_INSTANCE_H
