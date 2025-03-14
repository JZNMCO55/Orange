#include "pch.h"
#include "SceneCamera.h"

namespace Orange
{
    SceneCamera::SceneCamera()
        : Camera(glm::mat4(1.0f))  // 明确调用带参数的构造函数，传入单位矩阵
    {
        RecalculateProjection();
    }

    void SceneCamera::SetViewportSize(uint32_t width, uint32_t height)
    {
        ORANGE_CORE_ASSERT(width > 0 && height > 0, "Invalid viewport size");
        mAspectRatio = (float)width / (float)height;
        RecalculateProjection();
    }

    void SceneCamera::SetPerspective(float fov, float nearClip, float farClip)
    {
        mProjectionType = ProjectionType::Perspective;
        mPerspectiveFOV = fov;
        mPerspectiveNear = nearClip;
        mPerspectiveFar = farClip;
        RecalculateProjection();
    }

    void SceneCamera::SetPerspectiveFOV(float fov) noexcept
    {
        mPerspectiveFOV = fov;
        RecalculateProjection();
    }

    const float SceneCamera::GetPerspectiveFOV() const noexcept
    {
        return mPerspectiveFOV;
    }

    void SceneCamera::SetPerspectiveNear(float nearClip) noexcept
    {
        mPerspectiveNear = nearClip;
        RecalculateProjection();
    }

    const float SceneCamera::GetPerspectiveNear() const noexcept
    {
        return mPerspectiveNear;
    }

    void SceneCamera::SetPerspectiveFar(float farClip) noexcept
    {
        mPerspectiveFar = farClip;
        RecalculateProjection();
    }

    const float SceneCamera::GetPerspectiveFar() const noexcept
    {
        return mPerspectiveFar;
    }

    void SceneCamera::SetOrthographic(float size, float nearClip, float farClip) noexcept
    {
        mProjectionType = ProjectionType::Orthographic;
        mOrthographicSize = size;
        mOrthographicNear = nearClip;
        mOrthographicFar = farClip;
        RecalculateProjection();
    }

    void SceneCamera::SetOrthographicSize(float size) noexcept
    {
        mOrthographicSize = size;
        RecalculateProjection();
    }

    float SceneCamera::GetOrthographicSize() const noexcept
    {
        return mOrthographicSize;
    }

    void SceneCamera::SetOrthographicNear(float nearClip) noexcept
    {
        mOrthographicNear = nearClip;
        RecalculateProjection();
    }

    const float SceneCamera::GetOrthographicNear() const noexcept
    {
        return mOrthographicNear;
    }

    void SceneCamera::SetOrthographicFar(float farClip) noexcept
    {
        mOrthographicFar = farClip;
        RecalculateProjection();
    }

    const float SceneCamera::GetOrthographicFar() const noexcept
    {
        return mOrthographicFar;
    }

    void SceneCamera::SetProjectionType(SceneCamera::ProjectionType type) noexcept
    {
        mProjectionType = type;
        RecalculateProjection();
    }

    const SceneCamera::ProjectionType SceneCamera::GetProjectionType() const noexcept
    {
        return mProjectionType;
    }
    
    void SceneCamera::RecalculateProjection()
    {
        switch (mProjectionType)
        {
        case ProjectionType::Perspective:
        {
            mProjection = glm::perspective(mPerspectiveFOV, mAspectRatio, mPerspectiveNear, mPerspectiveFar);
        }
            break;
        case ProjectionType::Orthographic:
        {
            float orthoLeft = -mOrthographicSize * mAspectRatio * 0.5f;
            float orthoRight = mOrthographicSize * mAspectRatio * 0.5f;
            float orthoBottom = -mOrthographicSize * 0.5f;
            float orthoTop = mOrthographicSize * 0.5f;

            mProjection = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, mOrthographicNear, mOrthographicFar);
        }
            break;
        default:
            break;
        }
    }
    
}
