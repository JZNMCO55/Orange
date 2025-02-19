#include "pch.h"
#include "SceneCamera.h"

namespace Orange
{
    SceneCamera::SceneCamera()
        : Camera(glm::mat4(1.0f))  // 明确调用带参数的构造函数，传入单位矩阵
    {
        RecalculateProjection();
    }
    
    void SceneCamera::SetOrthographic(float size, float nearClip, float farClip)
    {
        mOrthographicSize = size;
        mOrthographicNear = nearClip;
        mOrthographicFar = farClip;
        RecalculateProjection();
    }

    void SceneCamera::SetViewportSize(uint32_t width, uint32_t height)
    {
        mAspectRatio = (float)width / (float)height;
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
    
    void SceneCamera::RecalculateProjection()
    {
        float orthoLeft = -mOrthographicSize * mAspectRatio * 0.5f;
        float orthoRight = mOrthographicSize * mAspectRatio * 0.5f;
        float orthoBottom = -mOrthographicSize * 0.5f;
        float orthoTop = mOrthographicSize * 0.5f;

        mProjectionMatrix = glm::ortho(orthoLeft, orthoRight, orthoBottom, orthoTop, mOrthographicNear, mOrthographicFar);    
    }
    
}
