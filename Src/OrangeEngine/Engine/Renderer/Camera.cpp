#include "pch.h"
#include "Camera.h"

namespace Orange
{
    Camera::Camera()
    {
    }
    Camera::Camera(const glm::mat4& projectionMatrix)
      : mProjection(projectionMatrix)
   {
   }
}