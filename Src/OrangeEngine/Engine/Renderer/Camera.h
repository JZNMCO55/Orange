#ifndef CAMERA_H
#define CAMERA_H

#include "OrangeExport.h"

namespace Orange
{
   class ORANGE_API Camera
   {
   public:
      Camera(const glm::mat4& projectionMatrix = glm::mat4(1.0f));

      const glm::mat4& GetProjectionMatrix() const { return mProjectionMatrix; }

   private:
      glm::mat4 mProjectionMatrix;
   };
}

#endif

