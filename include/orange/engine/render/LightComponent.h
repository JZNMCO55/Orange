#ifndef ORANGE_ENGINE_RENDER_LIGHT_COMPONENT_H
#define ORANGE_ENGINE_RENDER_LIGHT_COMPONENT_H

// ---------------------------------------------------------------------------
// LightComponent —— 光源 ECS component。
//
// 当前阶段只交付 `DirectionalLight` —— Ori 风格场
// 景的主光（太阳 / 月亮 / 卡通主光）就是它。点光 / 聚光留待后续扩展。
//
// 设计要点：
//   * **不存运行时矩阵**：light view / light proj 由 Pipeline 在 Shadow
//     Pass 实时算（按 light direction + scene bounding box），不放进
//     component；component 只存"输入数据" —— 方向 + 颜色 + 强度 + 是否
//     投影。让 ECS schema 紧凑、避免 player 修改 component 字段时把 view
//     矩阵设错。
//   * **direction 默认指向斜下方**：默认值在 main 函数里直接挂 component
//     就立即合理（典型卡通主光从前上方斜射），不强制调用方填字段。
//   * **不在 component 上加 priority / index 字段**：当前
//     Pipeline 取 first-found DirectionalLight 作为主光；多 light
//     的优先级 / 数量上限留待后续扩展。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/vec3.hpp>

namespace Orange::Engine::Render
{

// 平行光 ECS component。
//
// `direction` 约定为单位向量、指向"光的传播方向"（不是从表面指向光源——
// 与 GLSL 内置 light vector 约定相反）。Pipeline 会把它喂给 fragment
// shader 时按 `dot(N, -uLightDir)` 计算 NdotL，调用方不需要在每帧 normalize。
struct DirectionalLight
{
    // 默认值：从右上前方斜射下来，与 04_3d_mesh sample 的常用主光方向
    // 一致。调用方通常在 main 里覆盖这个字段。
    glm::vec3 direction{0.3f, -1.0f, 0.4f};

    // 线性 RGB 颜色（不预乘 intensity）。当前视觉基线下默认白光。
    glm::vec3 color{1.0f, 1.0f, 1.0f};

    // 标量强度乘子。Pipeline 在 fragment shader 里计算 lighting 时
    // 用 `color * intensity`。
    float intensity{1.0f};

    // 是否投影。false → Pipeline 跳过把这个 light 放进 shadow pass；
    // true 时 Pipeline 用第一个 castsShadow == true 的 light 计算 light
    // view-proj、跑 depth-only pass 渲到 shadow map。
    bool castsShadow{false};
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_LIGHT_COMPONENT_H
