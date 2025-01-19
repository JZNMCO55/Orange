#ifndef RENDERER_OBJECT_H
#define RENDERER_OBJECT_H

#include "OrangeExport.h"

namespace Orange
{
    class Mesh;
    class VertexArray;
    class ORANGE_API RendererObject
    {
    public:
        RendererObject();
        RendererObject(const Ref<Mesh>& tpMeshs);
        virtual ~RendererObject();

        void SetMesh(const Ref<Mesh>& tpMesh);
        const Ref<Mesh>& GetMesh() const;
    
        const Ref<VertexArray>& GetVertexArray();
    private:
        void MarkVertexArraysDirty();
        void ClearVertexArrayDirty();
        void UpdateVertexArray();
    private:
        Ref<Mesh> mpMesh;
        Ref<VertexArray> mpVertexArray;
        bool mbVertexArrayDirty;
    };

}
#endif // !RENDERER_OBJECT_H
