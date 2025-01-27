#include "pch.h"
#include "Renderer/OrthographicCamera.h"
#include "ApplicationEvent.h"
#include "MouseEvent.h"

#include "Timestep.h"
#include "KeyCodes.h"
#include "MouseButtonCodes.h"
#include "Input.h"
#include "OrthographicCameraControler.h"

namespace Orange
{
    OrthographicCameraControler::OrthographicCameraControler(float aspectRatio, bool rotationEnabled)
        : mAspectRatio(aspectRatio)
        , mbRotationEnabled(rotationEnabled)
        , mpCamera(std::make_shared<OrthographicCamera>(-mAspectRatio * mZoomLevel, mAspectRatio * mZoomLevel, -mZoomLevel, mZoomLevel))
    {
        mCameraPosition = mpCamera->GetPosition();
    }

    OrthographicCameraControler::~OrthographicCameraControler()
    {
    }

    void OrthographicCameraControler::OnUpdate(Timestep ts)
    {
        ORG_PROFILE_FUNCTION();
        if (Orange::Input::IsKeyPressed(ORG_KEY_A))
        {
            mCameraPosition.x += mCameraMoveSpeed * ts;
        }
        else if (Orange::Input::IsKeyPressed(ORG_KEY_D))
        {
            mCameraPosition.x -= mCameraMoveSpeed * ts;
        }

        if (Orange::Input::IsKeyPressed(ORG_KEY_W))
        {
            mCameraPosition.y -= mCameraMoveSpeed * ts;
        }
        else if (Orange::Input::IsKeyPressed(ORG_KEY_S))
        {
            mCameraPosition.y += mCameraMoveSpeed * ts;
        }

        if (mbRotationEnabled)
        {
            if (Orange::Input::IsKeyPressed(ORG_KEY_Q))
            {
                mCameraRotation += mCameraRotationSpeed * ts;
            }
            else if (Orange::Input::IsKeyPressed(ORG_KEY_E))
            {
                mCameraRotation -= mCameraRotationSpeed * ts;
            }
        }

        //mpCamera->SetPosition(mCameraPosition);
        mCameraMoveSpeed = mZoomLevel;
    }

    void OrthographicCameraControler::OnEvent(Event& e)
    {   
        ORG_PROFILE_FUNCTION();

        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>(ORANGE_BIND_EVENT_FN(OrthographicCameraControler::OnMouseScrolled));
        dispatcher.Dispatch<WindowResizeEvent>(ORANGE_BIND_EVENT_FN(OrthographicCameraControler::OnWindowResize));
        dispatcher.Dispatch<MouseMoveEvent>(ORANGE_BIND_EVENT_FN(OrthographicCameraControler::OnMouseMove));
    }

    bool OrthographicCameraControler::OnMouseScrolled(MouseScrolledEvent& e)
    {
        ORG_PROFILE_FUNCTION();

        mZoomLevel -= e.GetYOffset() * 0.25f;
        mZoomLevel = std::max(mZoomLevel, 0.25f);
        mpCamera->SetProjection(-mAspectRatio * mZoomLevel, mAspectRatio * mZoomLevel, -mZoomLevel, mZoomLevel);
        return false;
    }

    bool OrthographicCameraControler::OnWindowResize(WindowResizeEvent& e)
    {
        ORG_PROFILE_FUNCTION();

        mAspectRatio = (float)e.GetWidth() / (float)e.GetHeight();
        mpCamera->SetProjection(-mAspectRatio * mZoomLevel, mAspectRatio * mZoomLevel, -mZoomLevel, mZoomLevel);
        return false;
    }

    bool OrthographicCameraControler::OnMouseMove(MouseMoveEvent& e)
    {
        static float lastX = 0.0f;
        static float lastY = 0.0f;
        static bool firstMouse = true;

        if (firstMouse)
        {
            lastX = e.GetX();
            lastY = e.GetY();
            firstMouse = false;
        }

        float deltaX = e.GetX() - lastX;
        float deltaY = lastY - e.GetY();

        lastX = e.GetX();
        lastY = e.GetY();

        if (Input::IsMouseButtonPressed(ORG_MOUSE_BUTTON_MIDDLE))
        {
            auto [winX, winY] = Orange::Input::GetWindowSize();
            mpCamera->PanCamera(deltaX, deltaY, winX, winY);
        }
        return false;
    }
}