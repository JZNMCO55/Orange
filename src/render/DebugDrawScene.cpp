#include <orange/engine/render/DebugDrawScene.h>

#include <orange/engine/core/Log.h>

#include <orange/renderer/DebugDraw.h>
#include <orange/rhi/RHICommandList.h>
#include <orange/rhi/RHIDevice.h>
#include <orange/rhi/RHITypes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <cstdint>
#include <memory>

namespace Orange::Engine::Render
{

// 内部 Impl：持 OrangeRender DebugDraw 实例 + Pipeline 接通 / 开关状态。
// 公共头零 `<orange/...>` include 走前向声明路径，所有 OR 类型仅在 .cpp
// 内部 include。
struct DebugDrawScene::Impl
{
    Orange::Renderer::DebugDraw oRDebugDraw;
    bool                         initialized{false};
    bool                         enabled{true};
};

DebugDrawScene::DebugDrawScene() : mpImpl(std::make_unique<Impl>()) {}

DebugDrawScene::~DebugDrawScene()
{
    ShutdownBackend_();
}

// ---------------------------------------------------------------------------
// 公共 API
// ---------------------------------------------------------------------------

void DebugDrawScene::SetEnabled(bool enabled) noexcept
{
    mpImpl->enabled = enabled;
}

bool DebugDrawScene::IsEnabled() const noexcept
{
    return mpImpl->enabled;
}

bool DebugDrawScene::IsInitialized() const noexcept
{
    return mpImpl->initialized;
}

void DebugDrawScene::AddLine(const glm::vec3& from, const glm::vec3& to,
                              std::uint32_t colorABGR)
{
    if (!mpImpl->initialized || !mpImpl->enabled)
    {
        return;
    }
    mpImpl->oRDebugDraw.AddLine(from, to, colorABGR);
}

void DebugDrawScene::AddTriangle(const glm::vec3& a, const glm::vec3& b,
                                  const glm::vec3& c, std::uint32_t colorABGR)
{
    if (!mpImpl->initialized || !mpImpl->enabled)
    {
        return;
    }
    mpImpl->oRDebugDraw.AddTriangle(a, b, c, colorABGR);
}

void DebugDrawScene::AddAabb(const glm::vec3& min, const glm::vec3& max,
                              std::uint32_t colorABGR)
{
    if (!mpImpl->initialized || !mpImpl->enabled)
    {
        return;
    }

    // 8 个角点：用 z=0 / z=1 标记下标，分底面 / 顶面 / 立柱三组绘制。
    const glm::vec3 c000{min.x, min.y, min.z};
    const glm::vec3 c001{min.x, min.y, max.z};
    const glm::vec3 c010{min.x, max.y, min.z};
    const glm::vec3 c011{min.x, max.y, max.z};
    const glm::vec3 c100{max.x, min.y, min.z};
    const glm::vec3 c101{max.x, min.y, max.z};
    const glm::vec3 c110{max.x, max.y, min.z};
    const glm::vec3 c111{max.x, max.y, max.z};

    auto& dbg = mpImpl->oRDebugDraw;
    // 底面 4 条（Y = min.y）
    dbg.AddLine(c000, c100, colorABGR);
    dbg.AddLine(c100, c101, colorABGR);
    dbg.AddLine(c101, c001, colorABGR);
    dbg.AddLine(c001, c000, colorABGR);
    // 顶面 4 条（Y = max.y）
    dbg.AddLine(c010, c110, colorABGR);
    dbg.AddLine(c110, c111, colorABGR);
    dbg.AddLine(c111, c011, colorABGR);
    dbg.AddLine(c011, c010, colorABGR);
    // 立柱 4 条（沿 Y 连接底 / 顶面）
    dbg.AddLine(c000, c010, colorABGR);
    dbg.AddLine(c100, c110, colorABGR);
    dbg.AddLine(c101, c111, colorABGR);
    dbg.AddLine(c001, c011, colorABGR);
}

void DebugDrawScene::AddSphere(const glm::vec3& center, float radius,
                                std::uint32_t colorABGR, int segments)
{
    if (!mpImpl->initialized || !mpImpl->enabled)
    {
        return;
    }
    if (segments < 4)
    {
        segments = 4;
    }

    constexpr float kTwoPi = 6.283185307179586f;
    auto& dbg = mpImpl->oRDebugDraw;

    // 三个正交大圆（XY / XZ / YZ）线性近似 wireframe 球。每个圆 `segments`
    // 段线段连接；视觉与 LumixEngine `gpu/debug_draw` 同款做法（足够给 light
    // 范围 / collider bounds / camera frustum 体积可视化用，不追求平滑）。
    auto drawCircle = [&](const glm::vec3& axisU, const glm::vec3& axisV) {
        glm::vec3 prev = center + axisU * radius;
        for (int i = 1; i <= segments; ++i)
        {
            const float     a = (kTwoPi * static_cast<float>(i)) / static_cast<float>(segments);
            const glm::vec3 p = center + (axisU * std::cos(a) + axisV * std::sin(a)) * radius;
            dbg.AddLine(prev, p, colorABGR);
            prev = p;
        }
    };
    drawCircle({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});  // XY
    drawCircle({1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});  // XZ
    drawCircle({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});  // YZ
}

// ---------------------------------------------------------------------------
// Backend 接口（仅 Pipeline 调）
// ---------------------------------------------------------------------------

Result<void, ResultCode> DebugDrawScene::InitializeBackend_(
    ::Orange::Rhi::RHIDevice&    device,
    ::Orange::Rhi::TextureFormat colorFormat,
    std::uint32_t                framesInFlight)
{
    if (mpImpl->initialized)
    {
        return ResultCode::AlreadyInitialized;
    }

    Orange::Renderer::DebugDrawDesc desc{};
    desc.mColorFormat         = colorFormat;
    desc.mMaxLineVertices     = 16384;  // 富余值：v0.9 gizmo / camera frustum / bounds 估算上限 ~4096
    desc.mMaxTriangleVertices = 4096;
    desc.mFramesInFlight      = framesInFlight;
    desc.mpDebugName          = "orange_engine.debug_draw_scene";

    const auto rc = mpImpl->oRDebugDraw.Initialize(device, desc);
    if (rc != Orange::ResultCode::Success)
    {
        ORANGE_LOG_ERROR(
            "DebugDrawScene::Initialize: OrangeRender DebugDraw::Initialize 失败 "
            "(rc={})", static_cast<int>(rc));
        return ResultCode::InternalError;
    }

    mpImpl->initialized = true;
    return Result<void, ResultCode>{};
}

void DebugDrawScene::ShutdownBackend_() noexcept
{
    if (!mpImpl->initialized)
    {
        return;
    }
    mpImpl->oRDebugDraw.Shutdown();
    mpImpl->initialized = false;
}

void DebugDrawScene::SetViewProjBackend_(const glm::mat4& viewProj)
{
    if (!mpImpl->initialized)
    {
        return;
    }
    mpImpl->oRDebugDraw.SetViewProj(viewProj);
}

void DebugDrawScene::FlushBackend_(::Orange::Rhi::RHICommandList& cmd)
{
    if (!mpImpl->initialized || !mpImpl->enabled)
    {
        return;
    }
    mpImpl->oRDebugDraw.Flush(cmd);
}

bool DebugDrawScene::IsEmptyBackend_() const noexcept
{
    if (!mpImpl->initialized)
    {
        return true;
    }
    return mpImpl->oRDebugDraw.IsEmpty();
}

}  // namespace Orange::Engine::Render
