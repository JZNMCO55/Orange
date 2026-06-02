// TransformMath 的 headless 单元测试：local TRS ↔ world matrix 合成 / 分解。
// 锁住 ComposeLocalMatrix（T*R*S 单一真相源）+ DecomposeToLocalTransform（world→local，
// A1 gizmo 写回路径地基）的 round-trip 正确性。纯数学，无 World/GPU。

#include "orange/engine/scene/TransformMath.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace Scene = ::Orange::Engine::Scene;

namespace
{

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

bool NearV3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
{
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

// 四元数等价（含 q ≡ -q double-cover）：比较旋转作用于基向量是否一致。
bool SameRotation(const glm::quat& a, const glm::quat& b, float eps = 1e-3f)
{
    return NearV3(a * glm::vec3(1, 0, 0), b * glm::vec3(1, 0, 0), eps) &&
           NearV3(a * glm::vec3(0, 1, 0), b * glm::vec3(0, 1, 0), eps) &&
           NearV3(a * glm::vec3(0, 0, 1), b * glm::vec3(0, 0, 1), eps);
}

}  // namespace

int main()
{
    using Scene::TransformComponent;

    // ===== 1. identity → identity 矩阵 =====
    {
        const glm::mat4 m = Scene::ComposeLocalMatrix(TransformComponent{});
        assert(NearV3(glm::vec3(m * glm::vec4(0, 0, 0, 1)), glm::vec3(0)) && "identity 原点不动");
        std::fprintf(stdout, "  [PASS] ComposeLocalMatrix identity\n");
    }

    // ===== 2. 纯平移 =====
    {
        TransformComponent t;
        t.position = glm::vec3(1, 2, 3);
        const glm::mat4 m = Scene::ComposeLocalMatrix(t);
        assert(NearV3(glm::vec3(m * glm::vec4(0, 0, 0, 1)), glm::vec3(1, 2, 3)) && "平移把原点移到 pos");
        std::fprintf(stdout, "  [PASS] ComposeLocalMatrix 平移\n");
    }

    // ===== 3. round-trip（identity 父）：Compose → Decompose 还原 TRS =====
    {
        TransformComponent t;
        t.position = glm::vec3(1.0f, 2.0f, 3.0f);
        t.rotation = glm::quat(glm::radians(glm::vec3(0.0f, 45.0f, 0.0f)));
        t.scale    = glm::vec3(2.0f, 3.0f, 4.0f);

        const glm::mat4        m   = Scene::ComposeLocalMatrix(t);
        const TransformComponent got = Scene::DecomposeToLocalTransform(m, glm::mat4(1.0f));
        assert(NearV3(got.position, t.position) && "position 还原");
        assert(NearV3(got.scale, t.scale) && "scale 还原");
        assert(SameRotation(got.rotation, t.rotation) && "rotation 还原");
        std::fprintf(stdout, "  [PASS] round-trip（identity 父）TRS 还原\n");
    }

    // ===== 4. world→local（非 identity 父）：从 world 矩阵恢复 local =====
    {
        // 父在 (10,0,0) 绕 Y 90°。
        TransformComponent parent;
        parent.position    = glm::vec3(10.0f, 0.0f, 0.0f);
        parent.rotation    = glm::quat(glm::radians(glm::vec3(0.0f, 90.0f, 0.0f)));
        const glm::mat4 parentWorld = Scene::ComposeLocalMatrix(parent);

        TransformComponent localT;
        localT.position = glm::vec3(1.0f, 0.0f, 0.0f);
        localT.scale    = glm::vec3(1.5f, 1.5f, 1.5f);

        // 已知 local → 合成 world，再分解回 local 应一致。
        const glm::mat4 worldM = parentWorld * Scene::ComposeLocalMatrix(localT);
        const TransformComponent got = Scene::DecomposeToLocalTransform(worldM, parentWorld);
        assert(NearV3(got.position, localT.position, 1e-3f) && "local position 从 world 恢复");
        assert(NearV3(got.scale, localT.scale, 1e-3f) && "local scale 恢复");
        assert(SameRotation(got.rotation, localT.rotation) && "local rotation 恢复");
        std::fprintf(stdout, "  [PASS] world→local（非 identity 父）恢复 local\n");
    }

    // ===== 5. identity 父 → local == world（gizmo 零回归性质）=====
    {
        TransformComponent t;
        t.position = glm::vec3(5.0f, -2.0f, 7.0f);
        t.rotation = glm::quat(glm::radians(glm::vec3(30.0f, 0.0f, 0.0f)));
        const glm::mat4 worldM = Scene::ComposeLocalMatrix(t);  // 当作 gizmo 给的新 world
        const TransformComponent got = Scene::DecomposeToLocalTransform(worldM, glm::mat4(1.0f));
        assert(NearV3(got.position, t.position) && SameRotation(got.rotation, t.rotation) &&
               "identity 父：local==world（root 实体 gizmo 零回归）");
        std::fprintf(stdout, "  [PASS] identity 父 local==world\n");
    }

    std::fprintf(stdout, "TransformMathTest: all passed\n");
    return 0;
}
