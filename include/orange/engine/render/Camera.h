#ifndef ORANGE_ENGINE_RENDER_CAMERA_H
#define ORANGE_ENGINE_RENDER_CAMERA_H

// ---------------------------------------------------------------------------
// Camera —— 渲染所需的视图 + 投影矩阵对。
//
// 当前阶段保持极简：value-type 数据壳，两条静态工厂构造常见相机。可
// 直接作为 component 挂到 entity 上（与 RenderableComponent 同一思路），
// Pipeline 在每帧渲染前从 World 中取出对应实体的 Camera 数据。
//
// **NDC / handedness 约定**（与 OrangeRender / Vulkan 对齐）：
//   * 右手坐标系，World y-up；
//   * Vulkan NDC：x ∈ [-1, 1]，y ∈ [-1, 1]（y-down，与 OpenGL 反向），
//     z ∈ [0, 1]；
//   * 工厂内部直接手写矩阵，不依赖 `glm::perspective` / `glm::ortho`
//     的 GLM_FORCE_* 配置——保持调用点的数学行为可见、可移植。
//
// 详见 vendor/Orange-Wiki/wiki/concepts/foundations/matrix-transforms.md。
// ---------------------------------------------------------------------------

#include <glm/mat4x4.hpp>

#include <cmath>

namespace Orange::Engine::Render
{

struct Camera
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};

    // 透视投影 —— fovYRadians = 垂直视角（弧度），aspect = 宽高比，
    // zNear / zFar 为正数。生成的矩阵：右手输入 → Vulkan NDC（y-down，
    // z ∈ [0,1]）。
    static Camera Perspective(float fovYRadians,
                              float aspect,
                              float zNear,
                              float zFar) noexcept
    {
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        glm::mat4 p(0.0f);
        p[0][0] = f / aspect;
        p[1][1] = -f;                              // y-flip 至 Vulkan NDC
        p[2][2] = zFar / (zNear - zFar);           // z 映射到 [0,1]（近远反向）
        p[2][3] = -1.0f;                           // w = -view-z
        p[3][2] = (zNear * zFar) / (zNear - zFar);
        Camera cam;
        cam.projection = p;
        return cam;
    }

    // 正交投影 —— left/right/bottom/top 是世界空间盒子；y-flip 同样
    // 内置；z 映射到 [0,1]。
    static Camera Orthographic(float left,   float right,
                               float bottom, float top,
                               float zNear,  float zFar) noexcept
    {
        glm::mat4 p(1.0f);
        p[0][0] =  2.0f / (right - left);
        p[1][1] = -2.0f / (top - bottom);          // y-flip 至 Vulkan NDC
        p[2][2] =  1.0f / (zNear - zFar);          // z ∈ [0,1]
        p[3][0] = -(right + left) / (right - left);
        p[3][1] =  (top + bottom) / (top - bottom);
        p[3][2] =  zNear / (zNear - zFar);
        Camera cam;
        cam.projection = p;
        return cam;
    }
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_CAMERA_H
