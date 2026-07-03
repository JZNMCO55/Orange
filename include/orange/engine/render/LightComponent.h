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

    // 点光 ECS component。
    //
    // 位置由 entity 的 TransformComponent.position 派生 —— 与 DirectionalLight
    // 走 Transform.rotation 同款约定，组件不冗余存几何状态。
    //
    // 衰减模型采用物理基 inverse-square 加 smoothstep 至 range 截断：
    //   atten(d) = (1 / max(d², minD²)) * smoothstep(1, 0, d / range)
    // 其中 minD² = 0.01 防 0 除；smoothstep 让 range 边界平滑过渡到 0，避免
    // 硬边切割。Pipeline 在 fragment shader 内消费。
    //
    // 设计取舍：
    //   * 不在组件上挂 attenuation 系数 —— 物理基公式只一个 range 参数，与
    //     Cocos pointLight / Godot OmniLight 同款（"距离 + 强度"两参数即够）。
    //     classical (constant + linear*d + quadratic*d²) 模型有调参负担，PBR
    //     baseline 直接走物理基。
    //   * SpotLight 走独立 struct（见下方）—— 锥光 = point 衰减 × 锥角软边，
    //     语义与 point 不同，不复用同一组件。
    //   * castsShadow：true 时 Pipeline 为该 point light 烘 6 面 cubemap
    //     omnidirectional shadow（多 shadow caster 架构落地后启用）。
    struct PointLight
    {
        // 线性 RGB 颜色（不预乘 intensity）。
        glm::vec3 color{1.0f, 1.0f, 1.0f};

        // 标量强度乘子。Pipeline 在 fragment shader 里按
        // `color * intensity * attenuation(distance)` 累加贡献。
        float intensity{1.0f};

        // 光照影响距离上限（米）。超过此距离贡献被 smoothstep 截断到 0；
        // Pipeline 用 range 做 culling（距离 camera 太远的 PointLight 不进
        // light list）。典型室内点光 5-10m，路灯 / 灯塔 30-100m。
        float range{10.0f};

        // true 时 Pipeline 为该 point light 烘 6 面 cubemap omnidirectional
        // shadow（多 shadow caster 架构落地后生效）。
        bool castsShadow{false};

        // —— 可见光晕（GAP-2026-05-11 G3）——
        //
        // true 时 Pipeline 在 entity.Transform.position 画一个 emissive sphere
        // mesh，让用户在 viewport 看到光源本身（典型 Ori-like 发光主角 / 灯泡
        // prop 用法）。emissive 颜色 = color * intensity * haloIntensity，由
        // BloomPass 自然散光产生 glow（GAP G3 字面 "billboard 自发光 sphere
        // mesh + bloom 自动散光近似"，实现走 3D sphere 而非 billboard 让 halo
        // 在 PointLight 朝任意角度时都能从视线方向看到球面）。
        //
        // 与 castsShadow 正交（halo 只影响视觉表现，不参与光照 / 阴影计算）。
        bool haloEnabled{false};

        // halo sphere 的世界尺寸（米，半径）。典型 0.1-0.3m（灯泡级）。过大
        // 会让 halo 视觉吞掉真正的 mesh，过小会被 bloom 完全糊掉看不到形状。
        float haloRadius{0.15f};

        // halo emissive 强度的额外乘子（与 PointLight.intensity 独立）。
        // 1.0 = halo 视觉强度直接跟随 light intensity；>1 加强 halo glow（适合
        // 需要"小光大晕"的视觉风格）；<1 减弱 halo（适合"光强但本体不显眼"
        // 的场景）。最终 emissive 颜色 = color * intensity * haloIntensity。
        float haloIntensity{1.0f};
    };

    // 聚光 ECS component。
    //
    // 几何状态全部由 entity 的 TransformComponent 派生 —— position 由
    // `Transform.position`、direction 由 `Transform.rotation` 派生（identity
    // rotation 表示锥光向下，-Y，与 DirectionalLight 同款约定）。组件只存
    // "输入数据"，不冗余存位置 / 朝向。
    //
    // 着色模型 = PointLight 物理基 inverse-square × range smoothstep 衰减，
    // 再乘一个锥角软边因子：
    //   cone = smoothstep(cos(outerConeAngle), cos(innerConeAngle),
    //                     dot(spotDir, L))
    // 其中 spotDir 是锥光传播方向、L 是表面指向光源方向。innerConeAngle 内
    // 全亮、outerConeAngle 外全暗、两者之间平滑过渡（软锥边）。
    //
    // 设计取舍：
    //   * 锥角存 **半角弧度**（从中心轴到锥边的夹角）—— 与 glm::perspective
    //     的 fov = 2*outerConeAngle 直接对接（透视阴影 light proj 复用），
    //     且着色端只需 cos 一次。编辑器按 "(rad)" 标签暴露（与 RigidBody
    //     的 initialAngle 同款 radian 字段惯例）。
    //   * castsShadow：true 时 Pipeline 为该 spot 烘一张 perspective shadow
    //     map（多 shadow caster 架构落地后生效）。
    struct SpotLight
    {
        // 线性 RGB 颜色（不预乘 intensity）。
        glm::vec3 color{1.0f, 1.0f, 1.0f};

        // 标量强度乘子。
        float intensity{1.0f};

        // 光照影响距离上限（米）。超过此距离贡献被 smoothstep 截断到 0；
        // Pipeline 用 range 做 culling + 透视阴影 light proj 的 zFar。
        float range{15.0f};

        // 内锥半角（弧度，从中心轴量起）。≤ 此角全亮。默认 ~17°。
        float innerConeAngle{0.30f};

        // 外锥半角（弧度）。≥ 此角全暗；内外之间 smoothstep 软过渡。默认 ~26°。
        // 透视阴影 light proj 的 fov = 2*outerConeAngle。
        float outerConeAngle{0.45f};

        // true 时 Pipeline 为该 spot 烘一张 perspective shadow map
        //（多 shadow caster 架构落地后生效）。
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

    // identity rotation 下 SpotLight 的默认锥光传播方向（-Y，向下）——
    // 与 DirectionalLight 同款约定，让"旋转 entity 即改光向"在两种光之间
    // 体验一致。
    inline constexpr glm::vec3 kSpotLightLocalForward{0.0f, -1.0f, 0.0f};

    // 由 quaternion 推算锥光传播方向（已 normalize）。Pipeline（透视阴影
    // light view / 着色锥角）+ gizmo（锥体 wireframe）共用这一条公式。
    inline glm::vec3 ComputeSpotLightWorldDir(const glm::quat& rotation) noexcept
    {
        return glm::normalize(rotation * kSpotLightLocalForward);
    }

} // namespace Orange::Engine::Render

#endif // ORANGE_ENGINE_RENDER_LIGHT_COMPONENT_H
