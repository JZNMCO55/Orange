#ifndef SCENE_CAMERA_H
#define SCENE_CAMERA_H

#include "OrangeExport.h"
#include "Renderer/Camera.h"

namespace Orange
{
    class ORANGE_API SceneCamera : public Camera
    {
    public:
        enum ProjectionType
        {
            Perspective = 0, Orthographic = 1
        };
    public:
        SceneCamera();
        virtual ~SceneCamera() = default; 

        void SetViewportSize(uint32_t width, uint32_t height);
        
        // Perspective 
        void SetPerspective(float verticalFOV, float nearClip, float farClip);
        
        void SetPerspectiveFOV(float verticalFOV) noexcept;
        const float GetPerspectiveFOV() const noexcept;

        void SetPerspectiveNear(float nearClip) noexcept;
        const float GetPerspectiveNear() const noexcept;

        void SetPerspectiveFar(float farClip) noexcept;
        const float GetPerspectiveFar() const noexcept;

        // Orthographic
        void SetOrthographic(float size, float nearClip, float farClip) noexcept;

        void SetOrthographicSize(float size) noexcept;
        float GetOrthographicSize() const noexcept;

        void SetOrthographicNear(float nearClip) noexcept;
        const float GetOrthographicNear() const noexcept;

        void SetOrthographicFar(float farClip) noexcept;
        const float GetOrthographicFar() const noexcept;

        void SetProjectionType(ProjectionType type) noexcept;
        const ProjectionType GetProjectionType() const noexcept;

    private:
        void RecalculateProjection();
    private:
        ProjectionType mProjectionType = ProjectionType::Orthographic;

        float mPerspectiveFOV = glm::radians(45.0f);
        float mPerspectiveNear = 0.01f, mPerspectiveFar = 1000.0f;

        float mOrthographicSize = 10.0f;
        float mOrthographicNear = -1.0f, mOrthographicFar = 1.0f;
        float mAspectRatio = 0.0f;
    };
}
#endif