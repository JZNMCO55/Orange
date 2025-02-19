#ifndef SCENE_CAMERA_H
#define SCENE_CAMERA_H

#include "OrangeExport.h"
#include "Renderer/Camera.h"

namespace Orange
{
    class ORANGE_API SceneCamera : public Camera
    {
    public:
        SceneCamera();
        virtual ~SceneCamera() = default; 

        void SetOrthographic(float size, float nearClip, float farClip);
        void SetViewportSize(uint32_t width, uint32_t height);

        void SetOrthographicSize(float size) noexcept;
        float GetOrthographicSize() const noexcept;
    private:
        void RecalculateProjection();
    private:
        float mOrthographicSize = 10.0f;
        float mOrthographicNear = -1.0f, mOrthographicFar = 1.0f;
        float mAspectRatio = 0.0f;
    };
}
#endif