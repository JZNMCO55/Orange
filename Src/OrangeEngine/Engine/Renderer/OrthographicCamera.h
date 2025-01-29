#ifndef ORTHOGRAPHIC_CAMERA_H
#define ORTHOGRAPHIC_CAMERA_H

#include "OrangeExport.h"

namespace Orange
{
    class ORANGE_API OrthographicCamera
    {
    public:
        OrthographicCamera(float left, float right, float bottom, float top);
        
        // Set Camera Position
        void SetPosition(const glm::vec3& position);
        const glm::vec3& GetPosition() const { return mPosition; }

        // Set Camera Focus Point
        void SetFocusPoint(const glm::vec3& focusPoint);
        const glm::vec3& GetFocusPoint() const { return mFocusPoint; }

        // Set Camera Up Vector
        const glm::vec3& GetUp() const { return mUp; }

        void SetProjection(float left, float right, float bottom, float top);
        const glm::mat4& GetProjectionMatrix() const { return mProjectionMatrix;}
        const glm::mat4& GetViewMatrix();
        const glm::mat4& GetViewProjectionMatrix();

        void PanCamera(float deltaX, float deltaY, float windowWidth, float windowHeight);

        void RotateCamera(float deltaX, float deltaY, float sensitivity = 0.25f);
    private:
        // lazy evaluation
        void MarkDirty();
        void ClearDirty();
        void RecalculateViewMatrix();

    private:
        glm::mat4 mProjectionMatrix;
        glm::mat4 mViewMatrix;
        glm::mat4 mViewProjectionMatrix;
        glm::vec3 mPosition;
        glm::vec3 mFocusPoint;
        glm::vec3 mUp;
        glm::vec3 mForward;
        glm::vec3 mRightVec;
        float mLeft, mRight, mBottom, mTop;
        bool mbDirty;
        glm::quat mRotation;
        float mDistance;
    };
}


#endif // ORTHOGRAPHIC_CAMERA_H