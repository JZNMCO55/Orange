#include "pch.h"

#include "Mesh.h"
#include "Renderer/VertexArray.h"
#include "Renderer/RenderCommand.h"
#include "RendererObject.h"

namespace Orange
{
    RendererObject::RendererObject()
    {

    }

    RendererObject::RendererObject(const Ref<Mesh>& tpMeshs)
        : mpMesh(tpMeshs), mpVertexArray(nullptr), mbVertexArrayDirty(true)
    {

    }

    void RendererObject::SetMesh(const Ref<Mesh>& tpMesh)
    {
        mpMesh = tpMesh;
    }

    const Ref<Mesh>& RendererObject::GetMesh() const
    {
        return mpMesh;
    }

    const Ref<VertexArray>& RendererObject::GetVertexArray()
    {
        if (mbVertexArrayDirty)
        {
            UpdateVertexArray();
        }

        return mpVertexArray;
    }

    // Private
    void RendererObject::MarkVertexArraysDirty()
    {
        mbVertexArrayDirty = true;
    }

    void RendererObject::ClearVertexArrayDirty()
    {
        mbVertexArrayDirty = false;
    }

    void RendererObject::UpdateVertexArray()
    {
        if ((mpVertexArray == nullptr || mbVertexArrayDirty) && mpMesh!= nullptr)
        {
            mpVertexArray = VertexArray::Create();
            mpVertexArray->AddVertexBuffer(mpMesh->GetVertexBuffer());
            mpVertexArray->SetIndexBuffer(mpMesh->GetIndexBuffer());
            ClearVertexArrayDirty();
        }
    }

    RendererObject::~RendererObject()
    {

    }
}