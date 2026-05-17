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
//     component；component 只存"输入数据" —— 颜色 + 强度 + 是否投影。
//     让 ECS schema 紧凑、避免 player 修改 component 字段时把 view
//     矩阵设错。
//   * **方向不在 component 上 ——  由 entity 的 TransformComponent.rotation
//     派生**：identity rotation 表示光向下（-Y）；用户用 Rotate gizmo 转
//     entity 即改光向。原则与 Unity / Unreal / Godot 一致：几何状态（方向 /
//     位置 / 朝向）由 Transform 唯一拥有，component 不冗余保存。原本
//     component 上的 `direction` 字段（v1）已废，旧 scene 由
//     ComponentSerializers 内的 migrator 在 Load 时转换为 Transform.rotation
//   * **不在 component 上加 priority / index 字段**：当前
//     Pipeline 取 first-found DirectionalLight 作为主光；多 light
//     的优先级 / 数量上限留待后续扩展。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>

#include <glm/geometric.hpp>      // glm::normalize / glm::dot / glm::cross
#include <glm/gtc/quaternion.hpp> // glm::quat
#include <glm/vec3.hpp>

#include <cmath>

namespace Orange::Engine::Render
{

// 平行光 ECS component。
//
// 光的传播方向由 entity 的 TransformComponent.rotation 派生 ——
// `direction = rotation * (0, -1, 0)`。Pipeline 会把方向喂给 fragment
// shader 时按 `dot(N, -uLightDir)` 计算 NdotL，调用方不需要在每帧 normalize。
struct DirectionalLight
{
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

// identity rotation 下 DirectionalLight 的默认传播方向（-Y，向下）。
// 把 rotation * kDirectionalLightLocalForward 即得世界方向。
inline constexpr glm::vec3 kDirectionalLightLocalForward{0.0f, -1.0f, 0.0f};

// 由 quaternion 推算光向（已 normalize）。Pipeline 内消费 +
// gizmo / 工具代码共用这一条公式，避免散落多份"哪是 forward"的约定。
inline glm::vec3 ComputeDirectionalLightWorldDir(const glm::quat& rotation) noexcept
{
    return glm::normalize(rotation * kDirectionalLightLocalForward);
}

// 由世界方向反推 quaternion —— 调用方拿到旧 direction 字段（v1 scene
// migrator / 把传统 (0.3,-1,0.4) 形式默认值赋到 Transform.rotation 的
// sample / DemoWorld）需要这一步。返回的 quat 满足
// `ComputeDirectionalLightWorldDir(result) == normalize(desiredWorldDir)`
// （在数值精度内）。
//
// 实现：from-to 旋转的标准 quaternion 公式
// （见 Stan Melax, "The shortest arc quaternion"）—— 用 dot + cross +
// sqrt 直推半角 quat，避免拉 `<glm/gtx/quaternion.hpp>` 实验扩展的
// GLM_ENABLE_EXPERIMENTAL 宏污染消费者侧编译环境。
inline glm::quat MakeDirectionalLightRotationFromDir(const glm::vec3& desiredWorldDir) noexcept
{
    const glm::vec3 from = kDirectionalLightLocalForward;
    const glm::vec3 to   = glm::normalize(desiredWorldDir);
    const float     d    = glm::dot(from, to);
    if (d > 0.999999f)
    {
        // 完全同向 → identity
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (d < -0.999999f)
    {
        // 完全反向 (-Y → +Y)：绕 X 轴 180°。quat 表示 = (cos 90°, sin 90° * axis)
        // = (0, 1, 0, 0)。glm::quat 构造顺序 (w, x, y, z)。
        return glm::quat(0.0f, 1.0f, 0.0f, 0.0f);
    }
    const glm::vec3 axis = glm::cross(from, to);
    const float     s    = std::sqrt((1.0f + d) * 2.0f);
    const float     invS = 1.0f / s;
    return glm::quat(s * 0.5f, axis.x * invS, axis.y * invS, axis.z * invS);
}

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_LIGHT_COMPONENT_H
