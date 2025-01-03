#include "pch.h"
#include "RenderCommand.h"

#include "OpenGL/OpenGLRendererAPI.h"

namespace Orange
{
    RendererAPI* RenderCommand::spRendererAPI = new OpenGLRendererAPI();
}