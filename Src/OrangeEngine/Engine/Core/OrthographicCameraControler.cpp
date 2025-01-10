#include "pch.h"
#include "Renderer/OrthographicCamera.h"
#include "ApplicationEvent.h"
#include "MouseEvent.h"
#include "Timestep.h"
#include "KeyCodes.h"
#include "Input.h"
#include "OrthographicCameraControler.h"

namespace Orange
{
    OrthographicCameraControler::OrthographicCameraControler(float aspectRatio, bool rotationEnabled)
        : mAspectRatio(aspectRatio)
        , mbRotationEnabled(rotationEnabled)
        , mpCamera(std::make_shared<OrthographicCamera>(-mAspectRatio * mZoomLevel, mAspectRatio * mZoomLevel, -mZoomLevel, mZoomLevel))
    {
    }

    OrthographicCameraControler::~OrthographicCameraControler()
    {
    }

    void OrthographicCameraControler::OnUpdate(Timestep ts)
    {
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

        mpCamera->SetPosition(mCameraPosition);
        mCameraMoveSpeed = mZoomLevel;
    }

    void OrthographicCameraControler::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<MouseScrolledEvent>(ORANGE_BIND_EVENT_FN(OrthographicCameraControler::OnMouseScrolled));
        dispatcher.Dispatch<WindowResizeEvent>(ORANGE_BIND_EVENT_FN(OrthographicCameraControler::OnWindowResize));
    }

    bool OrthographicCameraControler::OnMouseScrolled(MouseScrolledEvent& e)
    {
        mZoomLevel -= e.GetYOffset() * 0.25f;
        mZoomLevel = std::max(mZoomLevel, 0.25f);
        mpCamera->SetProjection(-mAspectRatio * mZoomLevel, mAspectRatio * mZoomLevel, -mZoomLevel, mZoomLevel);
        return false;
    }

    bool OrthographicCameraControler::OnWindowResize(WindowResizeEvent& e)
    {
        mAspectRatio = (float)e.GetWidth() / (float)e.GetHeight();
        mpCamera->SetProjection(-mAspectRatio * mZoomLevel, mAspectRatio * mZoomLevel, -mZoomLevel, mZoomLevel);
        return false;
    }
}