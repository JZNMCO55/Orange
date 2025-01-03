#include "OpenGLPch.h"
#include "OpenGLContext.h"

namespace Orange
{
    OpenGLContext::OpenGLContext(GLFWwindow* tpWindowHandle) 
        : mpWindowHandle(tpWindowHandle)
    {
        ORANGE_CORE_ASSERT(mpWindowHandle, "Window handle is null!");
    }

    OpenGLContext::~OpenGLContext()
    {
    }

    void OpenGLContext::Init()
    {
        glfwMakeContextCurrent(mpWindowHandle);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        ORANGE_CORE_ASSERT(status, "Failed to initialize Glad!");
    }

    void OpenGLContext::SwapBuffers()
    {
        glfwSwapBuffers(mpWindowHandle);
    }
}