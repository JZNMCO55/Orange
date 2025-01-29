#include "pch.h"
#include "OrthographicCamera.h"

namespace Orange
{
    OrthographicCamera::OrthographicCamera(float left, float mRightVec, float bottom, float top)
        : mProjectionMatrix(glm::ortho(left, mRightVec, bottom, top, -10.0f, 10.0f))
        , mPosition(glm::vec3(0.0f , 0.0f, 2.0f))
        , mFocusPoint(glm::vec3(0.0f, 0.0f, 0.0f))
        , mUp(glm::vec3(0.0f, 1.0f, 0.0f))
        , mForward(glm::vec3(0.0f, 0.0f, -1.0f))
        , mRightVec(glm::vec3(1.0f, 0.0f, 0.0f))
        , mbDirty(true)
        , mLeft(left)
        , mRight(mRightVec)
        , mBottom(bottom)
        , mTop(top)
        , mDistance(2.0f)
        , mRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
    {
        ORG_PROFILE_FUNCTION();

        RecalculateViewMatrix();
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

    void OrthographicCamera::SetProjection(float left, float mRightVec, float bottom, float top)
    {
        ORG_PROFILE_FUNCTION();
        mProjectionMatrix = glm::ortho(left, mRightVec, bottom, top, -10.0f, 10.0f);
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
        glm::vec3 mForward = glm::normalize(mFocusPoint - mPosition);
        glm::vec3 mRightVec = glm::normalize(glm::cross(mForward, mUp));   // 右向量
        mUp = glm::cross(mRightVec, mForward);                    // 上向量

        // 计算移动向量：
        // - 水平方向：向右拖动（deltaX正）导致相机左移（-mRightVec方向）
        // - 垂直方向：向下拖动（deltaY正）导致相机上移（mUp方向）
        glm::vec3 moveVec = (-deltaX * worldUnitsPerPixelX) * mRightVec - (deltaY * worldUnitsPerPixelY) * mUp;

        // 更新相机的位置和焦点点
        mPosition += moveVec;
        mFocusPoint += moveVec;

        MarkDirty(); // 标记为需要重新计算视图矩阵
    }

    void OrthographicCamera::RotateCamera(float deltaX, float deltaY, float sensitivity) {
        ORG_PROFILE_FUNCTION();

        // 绕世界 Y 轴旋转（偏航）
        glm::quat yawRot = glm::angleAxis(glm::radians(-deltaX * sensitivity), mUp);
        // 绕本地右向量旋转（俯仰）
        glm::quat pitchRot = glm::angleAxis(glm::radians(deltaY * sensitivity), mRightVec);

        // 应用旋转（顺序：先俯仰后偏航）
        mRotation = yawRot * pitchRot * mRotation;
        mRotation = glm::normalize(mRotation);

        // 更新相机位置
        glm::vec3 initialOffset = glm::vec3(0.0f, 0.0f, mDistance);
        glm::vec3 rotatedOffset = mRotation * initialOffset;
        mPosition = mFocusPoint + rotatedOffset;

        // 动态更新上方向
        mForward = glm::normalize(mFocusPoint - mPosition);
        mRightVec = glm::normalize(glm::cross(mForward, mUp)); // 使用修正后的 mUp
        mUp = glm::normalize(glm::cross(mRightVec, mForward));

        MarkDirty();
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