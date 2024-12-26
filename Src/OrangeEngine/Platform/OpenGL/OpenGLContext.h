#ifndef OPENGL_CONTEXT_H
#define OPENGL_CONTEXT_H

#include "GraphicsContext.h"

namespace Orange
{
    class ORANGE_API OpenGLContext : public GraphicsContext
    {
    public:
        OpenGLContext(GLFWwindow* tpWindowHandle);
        ~OpenGLContext();

        virtual void Init() override;
        virtual void SwapBuffers() override;

    private:
        GLFWwindow* mpWindowHandle;
    };
}

#endif // OPENGL_CONTEXT_H