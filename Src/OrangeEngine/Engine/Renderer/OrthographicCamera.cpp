#include "pch.h"
#include "OrthographicCamera.h"

namespace Orange
{
    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
        : mProjectionMatrix(glm::ortho(left, right, bottom, top, -10.0f, 10.0f))
       , mPosition(glm::vec3(0.0f , 0.0f, 2.0f))
       , mFocusPoint(glm::vec3(0.0f, 0.0f, 0.0f))
       , mUp(glm::vec3(0.0f, 1.0f, 0.0f))
       , mbDirty(true)
       , mLeft(left)
       , mRight(right)
       , mBottom(bottom)
       , mTop(top)
    {
        ORG_PROFILE_FUNCTION();

        RecalculateViewMatrix();
        mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
    }

    void OrthographicCamera::SetPosition(const glm::vec3& position)
    {
        mPosition = position;
        MarkDirty();
    }

    void OrthographicCamera::SetFocusPoint(const glm::vec3& focusPoint)
    {
        mFocusPoint = focusPoint;
        MarkDirty();
    }
    
    void OrthographicCamera::SetUp(const glm::vec3& up)
    {
        mUp = up;
        MarkDirty();
    }

    void OrthographicCamera::SetProjection(float left, float right, float bottom, float top)
    {
        ORG_PROFILE_FUNCTION();
        mProjectionMatrix = glm::ortho(left, right, bottom, top, -10.0f, 10.0f);
        MarkDirty();
    }

    const glm::mat4& OrthographicCamera::GetViewMatrix()
    {
        if (mbDirty)
        {
            RecalculateViewMatrix();
        }
        return mViewMatrix;
    }
    const glm::mat4& OrthographicCamera::GetViewProjectionMatrix()
    {
        if (mbDirty)
        {
            RecalculateViewMatrix();
        }
        return mViewProjectionMatrix;
    }

    void OrthographicCamera::PanCamera(float deltaX, float deltaY,float windowWidth, float windowHeight)
    {
        if (windowWidth == 0 || windowHeight == 0)
        {
            return;
        }
        // 计算每个像素对应的世界单位
        float worldUnitsPerPixelX = (mRight - mLeft) / windowWidth;
        float worldUnitsPerPixelY = (mTop - mBottom) / windowHeight;

        // 计算相机的方向向量
        glm::vec3 forward = glm::normalize(mFocusPoint - mPosition);
        glm::vec3 mRight = glm::normalize(glm::cross(forward, mUp));   // 右向量
        glm::vec3 mUp = glm::cross(mRight, forward);                    // 上向量

        // 计算移动向量：
        // - 水平方向：向右拖动（deltaX正）导致相机左移（-right方向）
        // - 垂直方向：向下拖动（deltaY正）导致相机上移（up方向）
        glm::vec3 moveVec = (-deltaX * worldUnitsPerPixelX) * mRight - (deltaY * worldUnitsPerPixelY) * mUp;

        // 更新相机的位置和焦点点
        mPosition += moveVec;
        mFocusPoint += moveVec;

        MarkDirty(); // 标记为需要重新计算视图矩阵
    }

    // Private
    void OrthographicCamera::MarkDirty()
    {
        mbDirty = true;
    }

    void OrthographicCamera::ClearDirty()
    {
        mbDirty = false;
    }

    void OrthographicCamera::RecalculateViewMatrix()
    {
        ORG_PROFILE_FUNCTION();

        mViewMatrix = glm::lookAt(mPosition, mFocusPoint, mUp);
        mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;

        ClearDirty();
    }
}