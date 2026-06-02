#ifndef ORANGE_ENGINE_SCENE_TRANSFORM_MATH_H
#define ORANGE_ENGINE_SCENE_TRANSFORM_MATH_H

// ---------------------------------------------------------------------------
// TransformMath —— local TRS ↔ world matrix 的纯数学工具（header-only inline）。
//
// 背景：local TRS → mat4 的合成顺序（T*R*S，列向量惯例 worldVec=M*localVec）此前
// 在 TransformSystem.cpp（LocalMatrix）与 RenderScene（ComposeWorldMatrix）各有
// 一份**重复**定义，消费者无法复用。本头把它沉淀成单一真相源，并补上反向的
// world→local 分解——A1 transform gizmo 的"写回"路径（gizmo 给新 world 位姿，需
// 转成 local TRS 写进 TransformComponent）的引擎层地基（见
// docs/A1-gizmo-physics-hierarchy-followup.md）。
//
// 纯函数、无 World/GPU 依赖，headless 可测（compose→decompose round-trip）。
// ---------------------------------------------------------------------------

#include <orange/engine/scene/TransformComponent.h>

#include <glm/gtc/matrix_transform.hpp>  // glm::translate / glm::scale
#include <glm/gtc/quaternion.hpp>        // glm::mat4_cast / glm::quat_cast
#include <glm/geometric.hpp>             // glm::length / glm::normalize
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>                // glm::inverse

namespace Orange::Engine::Scene
{

// local TRS → mat4（合成顺序 T*R*S）。TransformSystem / RenderScene / gizmo 共用的
// 单一真相源。
inline glm::mat4 ComposeLocalMatrix(const TransformComponent& t)
{
    glm::mat4 m(1.0f);
    m = glm::translate(m, t.position);
    m = m * glm::mat4_cast(t.rotation);
    m = glm::scale(m, t.scale);
    return m;
}

// world matrix + 父 world → local TRS：localMatrix = inverse(parentWorld) * world，
// 再分解回 position / rotation / scale。parentWorld = identity 时 local == world。
//
// 限制：假设 local 是 T*R*S 合成（无 shear）。负 scale 无法与旋转区分（任意 TRS
// 分解的固有歧义），gizmo 场景下 scale 恒正可忽略。scale 分量近 0 时该轴退化为
// 不归一化（避免除零），rotation 取剩余可解部分。
inline TransformComponent DecomposeToLocalTransform(const glm::mat4& worldMatrix,
                                                    const glm::mat4& parentWorld)
{
    const glm::mat4 local = glm::inverse(parentWorld) * worldMatrix;

    TransformComponent out;
    out.position = glm::vec3(local[3]);

    const glm::vec3 c0 = glm::vec3(local[0]);
    const glm::vec3 c1 = glm::vec3(local[1]);
    const glm::vec3 c2 = glm::vec3(local[2]);
    out.scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));

    constexpr float kEps = 1e-8f;
    const glm::mat3 rotMat(out.scale.x > kEps ? c0 / out.scale.x : c0,
                           out.scale.y > kEps ? c1 / out.scale.y : c1,
                           out.scale.z > kEps ? c2 / out.scale.z : c2);
    out.rotation = glm::normalize(glm::quat_cast(rotMat));
    return out;
}

}  // namespace Orange::Engine::Scene

#endif  // ORANGE_ENGINE_SCENE_TRANSFORM_MATH_H
