#ifndef RENDERER_3D_H
#define RENDERER_3D_H

#include "OrangeExport.h"

namespace Orange
{
    class OrthographicCamera;
    class RendererObject;
    class ORANGE_API Renderer3D
    {
    public:
        static void Init();

        static void Shutdown();

        static void BeginScene(const Ref<OrthographicCamera>& camera);

        static void EndScene();

        static void RenderObject(const Ref<RendererObject>& object);
        
        static void RendererInstanceObject();
        
        static void Flush();


    };
}

#endif // RENDERER_3D_H