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
        void SetUp(const glm::vec3& up);
        const glm::vec3& GetUp() const { return mUp; }

        void SetProjection(float left, float right, float bottom, float top);
        const glm::mat4& GetProjectionMatrix() const { return mProjectionMatrix;}
        const glm::mat4& GetViewMatrix();
        const glm::mat4& GetViewProjectionMatrix();

        void PanCamera(float deltaX, float deltaY, float windowWidth, float windowHeight);
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
        float mLeft, mRight, mBottom, mTop;
        bool mbDirty;
    };
}


#endif // ORTHOGRAPHIC_CAMERA_H