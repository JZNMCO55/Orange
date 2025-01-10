#ifndef ORTHOGRAPHIC_CAMERA_CONTROLER_H
#define ORTHOGRAPHIC_CAMERA_CONTROLER_H

#include "OrangeExport.h"

namespace Orange
{
    class Timestep;
    class OrthographicCamera;
    class Event;
    class MouseScrolledEvent;
    class WindowResizeEvent;

    class ORANGE_API OrthographicCameraControler
    {
    public:
        OrthographicCameraControler(float aspectRatio, bool rotationEnabled = false);
        ~OrthographicCameraControler();

        void OnUpdate(Timestep ts);

        void OnEvent(Event& e);

        Ref<OrthographicCamera> GetCamera() const { return mpCamera; }

        void SetZoomLevel(float zoomLevel) { mZoomLevel = zoomLevel; }
        const float GetZoomLevel() const { return mZoomLevel; }

    private:
        bool OnMouseScrolled(MouseScrolledEvent& e);
        bool OnWindowResize(WindowResizeEvent& e);
    private:
        bool mbRotationEnabled;
        float mAspectRatio;
        float mZoomLevel = 1.0f;

        Ref<OrthographicCamera> mpCamera;
        glm::vec3 mCameraPosition = { 0.0f, 0.0f, 0.0f };
        float mCameraRotation = 0.0f;
        float mCameraMoveSpeed = 5.0f;
        float mCameraRotationSpeed = 180.0f;
    };
}

#endif // ORTHOGRAPHIC_CAMERA_CONTROLER_H