#ifndef ORANGE_ENGINE_RENDER_MATERIAL_INSTANCE_H
#define ORANGE_ENGINE_RENDER_MATERIAL_INSTANCE_H

// ---------------------------------------------------------------------------
// MaterialInstance —— Material 模板的单实例覆盖。
//
// 每个 MaterialInstance 绑定一个 const Material*（caller-owned），并维
// 护一份 per-instance 的 uniform / texture 覆盖。Pipeline 在每帧渲染时
// 先从 Material 取默认值（Phase 3 / Task 02 起），再用 Instance 的覆盖
// 增量补上——典型用法：同一 Material 模板被多个 entity 共享，每个 entity
// 通过自己的 MaterialInstance 覆盖个别 uniform（颜色 / 时间 / 纹理）。
//
// SetUniform / SetTexture 的 name 与 binding 必须在 Material 的描述符
// 列表里出现且类型匹配；否则调用是 no-op（不抛、不崩、不 log——0.x
// 阶段优先吞 schema 演化造成的"旧调用方喂新 template"路径）。Phase 3 /
// Task 02 内置第一个 template 后，如果发现 silent-ignore 隐藏了真 bug，
// 再考虑加可选 ORANGE_LOG_WARN。
//
// PIMPL：override 存储用 unordered_map + type-tagged blob，藏在 .cpp，
// 公共头不暴露 std::variant<glm 多类型> 这种"传染性"的复合 std 类型。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/render/Material.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace Orange::Engine::Render
{

class ORANGE_ENGINE_API MaterialInstance
{
public:
    // pMaterial == nullptr 是受支持的退化状态（"还没 bind template 的
    // 半构造态"）；所有 SetXxx 在这种状态下都是 no-op。Phase 3 / Task 02
    // 起把它和 MaterialSystem::CreateInstance 配套，正常路径下不会出现
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

    // 单元测试 / Pipeline 内部用的存在性查询。Phase 3 / Task 02 起会再
    // 加 read-back 接口（取覆盖值用于 push-constant / UBO 写入）；当前
    // 阶段的最小 surface 只暴露 has-/no- 二态。
    bool HasUniformOverride(std::string_view name) const noexcept;
    bool HasTextureOverride(std::uint32_t binding) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_MATERIAL_INSTANCE_H
