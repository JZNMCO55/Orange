#ifndef ORANGE_RENDERCORE_H
#define ORANGE_RENDERCORE_H

// Camera System
#include "Camera/Camera.h"
#include "Camera/CameraController.h"

// Geometry System
#include "Geometry/Vertex.h"
#include "Geometry/Mesh.h"
#include "Geometry/MeshGenerator.h"

namespace Orange
{
    namespace RenderCore
    {
        /**
         * @brief RenderCore 统一初始化
         * 初始化渲染核心系统的所有组件
         */
        bool Initialize();

        /**
         * @brief RenderCore 清理
         * 清理渲染核心系统的所有组件
         */
        void Shutdown();

        /**
         * @brief 获取版本信息
         */
        const char *GetVersion();
    }
}

#endif // ORANGE_RENDERCORE_H