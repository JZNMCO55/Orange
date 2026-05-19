#ifndef ORANGE_ENGINE_RENDER_DEBUG_DRAW_SCENE_H
#define ORANGE_ENGINE_RENDER_DEBUG_DRAW_SCENE_H

// ---------------------------------------------------------------------------
// DebugDrawScene —— viewport 调试几何 immediate-mode API。
//
// 用法（编辑器 / sample 端）：
//   auto* dbg = pipeline.GetDebugDrawScene();
//   if (dbg) {
//       dbg->AddLine(p0, p1, 0xFF0000FFu);     // ABGR packed：低 8 位 = R
//       dbg->AddAabb(min, max, 0xFF00FF00u);
//       dbg->AddSphere(center, 0.5f, 0xFFFFFFFFu);
//   }
//
// 生命周期：实例由 Pipeline 持有，Pipeline::Initialize /
// InitializeOffscreen 后自动 ready；Pipeline::Shutdown 时释放。消费者
// 不构造、不析构本类型。每帧 Add* 累积顶点；Pipeline::Render 在主 pass +
// 粒子 + grid 之后、passthrough/tonemap 之前自动 SetViewProj + Flush，
// 输出到 HDR target。
//
// 不写深度——debug 几何始终覆盖在已 shade 的几何之上，与 Lumix /
// Godot debug viewport 同款 always-on-top 节奏。颜色端走 HDR RGBA16F 自
// 然参与 tonemap，亮度 > 1 时进 bloom。
//
// 颜色编码：mColorABGR 为 packed uint32，低 8 位 = R，高 8 位 = A
// （0xAA_BB_GG_RR）。与 OrangeRender `Renderer::DebugVertex::mColorABGR`
// 同 endian 约定，可直接互通。
//
// `SetEnabled(false)` 时 Add* 静默丢弃，Pipeline 跳过 Flush——为编辑器
// ScenePanel toolbar "Debug Draw" checkbox 提供 zero-cost off 路径。
//
// 公共头**不**包含任何 OrangeRender / Vulkan 头：依据 CLAUDE.md 的
// "Header isolation" 不变量，`<orange/rhi/...>` 类型仅以前向声明形式出
// 现于参数列表。Backend 操作（InitializeBackend_ / FlushBackend_ 等）由
// `friend class Pipeline` 访问，消费者不直接调用。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>

// OrangeRender 公共面前向声明 —— 仅用于参数类型，公共头不 `#include`。
namespace Orange::Rhi
{
class RHIDevice;
class RHICommandList;
enum class TextureFormat : std::uint32_t;
}

namespace Orange::Engine::Render
{

class Pipeline;

class ORANGE_ENGINE_API DebugDrawScene
{
public:
    DebugDrawScene();
    ~DebugDrawScene();

    DebugDrawScene(const DebugDrawScene&)            = delete;
    DebugDrawScene& operator=(const DebugDrawScene&) = delete;

    // 线段：from → to 一条 colored line。颜色按 packed ABGR 编码。
    void AddLine(const glm::vec3& from, const glm::vec3& to,
                 std::uint32_t colorABGR);

    // Axis-Aligned Bounding Box：12 条 wireframe 线段构成。`min` / `max`
    // 必须 `min.x ≤ max.x` 等三轴成立；否则视觉退化为退化盒子但不 crash。
    void AddAabb(const glm::vec3& min, const glm::vec3& max,
                 std::uint32_t colorABGR);

    // Wireframe 球：用 XY / XZ / YZ 三个大圆近似可视化。`segments` 是单
    // 圆分段数（默认 12，共 36 条线）；< 4 静默截到 4。
    void AddSphere(const glm::vec3& center, float radius,
                   std::uint32_t colorABGR, int segments = 12);

    // 填充三角形（实心，不走 wireframe；不背面剔除）。
    void AddTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                     std::uint32_t colorABGR);

    // 全局开关。disabled 时 Add* 静默丢，Pipeline 跳过 Flush。
    void SetEnabled(bool enabled) noexcept;
    bool IsEnabled() const noexcept;

    // Pipeline 对接 RHI 后转 true；Initialize 失败 / 未调用 / 已 Shutdown
    // 时为 false。消费者通常无需检测——Add* 自身对未 Initialize 静默。
    bool IsInitialized() const noexcept;

private:
    friend class Pipeline;

    // Backend 接口仅供 `Pipeline` 调用。参数为 OrangeRender 公共面类型
    // （前向声明）；消费者不直接调用本组方法。
    Result<void, ResultCode> InitializeBackend_(
        ::Orange::Rhi::RHIDevice&      device,
        ::Orange::Rhi::TextureFormat   colorFormat,
        std::uint32_t                  framesInFlight);
    void ShutdownBackend_() noexcept;
    void SetViewProjBackend_(const glm::mat4& viewProj);
    void FlushBackend_(::Orange::Rhi::RHICommandList& cmd);
    bool IsEmptyBackend_() const noexcept;

    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Render

#endif  // ORANGE_ENGINE_RENDER_DEBUG_DRAW_SCENE_H
