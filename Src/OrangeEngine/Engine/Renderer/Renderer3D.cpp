#include "pch.h"

#include "OrthographicCamera.h"
#include "VertexArray.h"
#include "Shader.h"
#include "RenderCommand.h"
#include "Geometry/RendererObject.h"
#include "Renderer3D.h"

namespace Orange
{
    struct Renderer3DData
    {
        Ref<Shader> mpFlatShader;
    };
    static Renderer3DData sData;

    void Renderer3D::Init()
    {
        sData.mpFlatShader = Shader::Create(R"(../../Resource/Shaders/FlatShader.glsl)");
    }

    void Renderer3D::Shutdown()
    {

    }

    void Renderer3D::BeginScene(const Ref<OrthographicCamera>& camera)
    {
        sData.mpFlatShader->Bind();
        sData.mpFlatShader->SetMat4("u_ViewProjection", glm::mat4(1.0f));
    }

    void Renderer3D::EndScene()
    {

    }

    void Renderer3D::RenderObject(const Ref<RendererObject>& object)
    {
        if (!object)
        {
            return;
        }
        object->GetVertexArray()->Bind();
        RenderCommand::DrawIndexed(object->GetVertexArray());
    }
}