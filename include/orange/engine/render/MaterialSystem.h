#ifndef ORANGE_ENGINE_RENDER_MATERIAL_SYSTEM_H
#define ORANGE_ENGINE_RENDER_MATERIAL_SYSTEM_H

// ---------------------------------------------------------------------------
// MaterialSystem —— Material 模板的注册 / 查询 / 实例化容器。
//
// 把游戏侧自定义 shader 注入到引擎的扩展点：游戏在自己的 build 里用
// glslangValidator 预编 .spv，构造 `ShaderTemplateDesc`（name + 顶点/片段
// SPIR-V 路径 + uniform 列表 + 纹理槽列表）调 `RegisterTemplate`，引擎按
// path 走 AssetRegistry::Load<ShaderAsset> 加载、装配 Material 落进系统
// 内表；之后 `FindTemplate` / `CreateInstance` 即可消费。
//
// 设计要点：
//   * 实例化模式（值构造的类，调用方自己持有），与 PostProcessChain 同
//     节奏；不挂 AppHost、不走 singleton。需要时升级到 `app.GetMaterialSystem()`
//     不破公共面。
//   * 持 `Asset::AssetRegistry&` 引用作为成员——子系统需要长期访问 registry
//     时在构造时绑定，避免每次 RegisterTemplate 重新传参。registry 必须
//     活到 MaterialSystem 析构。
//   * 只接受 SPIR-V 路径。运行时 GLSL 编译 / hot-reload 留待后续扩展。
//   * `FindTemplate` 返回 `const Material*`：Material 在内表里地址稳定
//     （unordered_map 节点存储），新 RegisterTemplate 不会让旧引用失效。
//   * `CreateInstance` 返回 `std::unique_ptr<MaterialInstance>`：调用方
//     持有所有权；绑定的 const Material* 由 system 表保活——只要 system
//     活着，所有由它创建的 instance 都安全。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/render/Material.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialTypes.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Asset
{
class AssetRegistry;
}  // namespace Orange::Engine::Asset

namespace Orange::Engine::Render
{

// 自定义 shader 模板的描述符。所有字段都必须由调用方显式声明——引擎
// 不在运行时反射 SPIR-V 自动推导 uniform / texture 槽。
struct ShaderTemplateDesc
{
    // 模板的唯一名。FindTemplate / CreateInstance 按这个 name 索引；
    // 重名注册返回 ResultCode::AlreadyExists，表内不被覆盖。
    std::string name;

    // 顶点 / 片段 SPIR-V 路径。可以是绝对路径或工作目录相对路径——
    // RegisterTemplate 直接拿原 path 喂 AssetRegistry::Load<ShaderAsset>，
    // dedup 也按这个 path 做（内置 toon/rim_light 走 .exe-相对路径解析，
    // 自定义 shader 由调用方自己决定路径风格）。
    std::filesystem::path vertexSpirvPath;
    std::filesystem::path fragmentSpirvPath;

    // uniform / texture 槽布局，与 GLSL push_constant block 字段或
    // descriptor binding 一一对应。
    std::vector<MaterialUniformDesc>     uniforms;
    std::vector<MaterialTextureSlotDesc> textureSlots;
};

class ORANGE_ENGINE_API MaterialSystem
{
public:
    // 构造时绑定 registry 引用并存为成员；registry 必须活到 system 析构。
    explicit MaterialSystem(Asset::AssetRegistry& registry);
    ~MaterialSystem();

    MaterialSystem(const MaterialSystem&)            = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    MaterialSystem(MaterialSystem&&) noexcept;
    MaterialSystem& operator=(MaterialSystem&&) noexcept;

    // 注册一个新的模板。
    //   * desc.name 重复 → 返回 AlreadyExists，表内不被覆盖；
    //   * SPIR-V 加载失败 → 仍把 template 落地（uniform / texture 描述符
    //     保留，shader handle 用无效 handle，与 BuiltinMaterials::LoadToon
    //     失败语义一致）但返回 IoError，调用方可以 ignore；
    //   * 成功 → 返回 Ok，FindTemplate 后续命中。
    Result<void, ResultCode> RegisterTemplate(const ShaderTemplateDesc& desc);

    // 按名取得模板。未注册 → 返回 nullptr。返回的指针在 system 活着期间
    // 稳定（unordered_map 节点存储不会因 rehash 失效）。
    const Material* FindTemplate(std::string_view name) const noexcept;

    // 创建一个绑定到指定模板的 MaterialInstance。
    //   * name 未注册 → 返回 nullptr；
    //   * 成功 → 返回拥有式 unique_ptr，调用方负责让它在 system 之前析构
    //     （MaterialInstance 析构不依赖 Material，但仍是良好实践）。
    std::unique_ptr<MaterialInstance> CreateInstance(std::string_view name);

    std::size_t TemplateCount() const noexcept;

    // 枚举所有已注册模板的名字。返回拷贝（值类型 vector<string>），调用
    // 方持有的副本不会被后续 RegisterTemplate 的 rehash 影响——string_view
    // 版本曾被考虑但因 unordered_map 的 key 在 rehash 时仍可能让 view
    // 悬挂被 reject，0.x 阶段优先稳。
    //
    // 顺序未定义：内部是 unordered_map，遍历顺序与插入顺序无关。调用方
    // （如 Editor Inspector Combo 控件）若需稳定顺序，自行 sort。
    //
    // 不在帧内热路径——本接口典型消费方是 Editor UI / 资源序列化，与
    // FindTemplate / CreateInstance 同节奏；不强调零分配。
    std::vector<std::string> GetTemplateNames() const;

    // 便利方法：把引擎内置的 toon / rim_light 模板注册进 system。等价
    // 于手动 RegisterTemplate(toon_desc) + RegisterTemplate(rim_light_desc)，
    // 但内部直接复用 BuiltinMaterials::LoadToon / LoadRimLight 的工厂
    // （它们已经做完 SPIR-V 加载 + Material 装配），不重复实现。
    //
    // 重复调用幂等吗？不——第二次会因为 name 已存在返回 AlreadyExists。
    // 调用方按需在 system 生命周期早期调一次。
    Result<void, ResultCode> RegisterBuiltins();

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_MATERIAL_SYSTEM_H
