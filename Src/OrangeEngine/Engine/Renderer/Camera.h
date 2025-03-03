#ifndef CAMERA_H
#define CAMERA_H

#include "OrangeExport.h"

namespace Orange
{
   class ORANGE_API Camera
   {
   public:
      Camera();
      virtual ~Camera() = default;
      Camera(const glm::mat4& projectionMatrix = glm::mat4(1.0f));

      const glm::mat4& GetProjectionMatrix() const { return mProjection; }

   protected:
      glm::mat4 mProjection;
   };
}

#endif

