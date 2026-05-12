#ifndef ORANGE_ENGINE_RENDER_MATERIAL_TYPES_H
#define ORANGE_ENGINE_RENDER_MATERIAL_TYPES_H

// ---------------------------------------------------------------------------
// MaterialTypes —— Material / MaterialInstance 共用的描述符与枚举。
//
// 当前覆盖最小 uniform 类型集（Float / Vec2 / Vec3 / Vec4 / Int / Mat4），
// 刚好够支持内置卡通 + rim light template 与
// 自定义 shader 注入路径。需要新类型时（例如 IVec4、Bool、
// Texture-flag 等）在这里加 enum + 在 MaterialInstance 内 SetUniform
// 重载补一个对应签名即可，不破已有公共面。
//
// **不引入 SPIR-V 反射**：与"完全手写、不引入运行时反射库"
// 的反射策略对齐——uniform 与 texture 槽必须由 Material 创建方显式声
// 明，引擎不在运行时去解析 .spv 自动推断。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <cstdint>
#include <string>

namespace Orange::Engine::Render
{

// uniform 标量 / 向量 / 矩阵 类型枚举。Mat4 是当前最大宽度（16 floats =
// 64 字节），MaterialInstance 内部 uniform value 存储按 64 字节定长
// 对齐——加新类型时如果突破这个上限要同步调整 PIMPL 里的 blob 容量。
enum class MaterialUniformType : std::uint8_t
{
    Float,
    Vec2,
    Vec3,
    Vec4,
    Int,
    Mat4,
};

// 单个 uniform 的声明记录。`name` 与 GLSL / SPIR-V 中的 uniform 标识符
// 一一对应；MaterialInstance::SetUniform 按 name 查找。
struct MaterialUniformDesc
{
    std::string         name;
    MaterialUniformType type{MaterialUniformType::Float};
};

// 单个 texture 绑定槽的声明记录。`binding` 是 descriptor-set binding
// index（直接喂给 OrangeRender 的 descriptor
// 路径），`name` 仅作为 debug / 序列化用途。
struct MaterialTextureSlotDesc
{
    std::uint32_t binding{0};
    std::string   name;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_MATERIAL_TYPES_H
