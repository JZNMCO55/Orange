#include "pch.h"
#include "OrthographicCamera.h"

namespace Orange
{
    OrthographicCamera::OrthographicCamera(float left, float right, float bottom, float top)
        : mProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), mViewMatrix(1.0f)
    {
        ORG_PROFILE_FUNCTION();

        mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
    }
    void OrthographicCamera::SetProjection(float left, float right, float bottom, float top)
    {
        ORG_PROFILE_FUNCTION();

        mProjectionMatrix = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
        mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
    }
    void OrthographicCamera::RecalculateViewMatrix()
    {
        ORG_PROFILE_FUNCTION();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), mPosition) *
            glm::rotate(glm::mat4(1.0f), glm::radians(mRotation), glm::vec3(0, 0, 1));
        mViewMatrix = glm::inverse(transform);
        mViewProjectionMatrix = mProjectionMatrix * mViewMatrix;
    }
}