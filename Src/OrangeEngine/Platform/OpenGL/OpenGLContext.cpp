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
        ORG_PROFILE_FUNCTION();

        glfwMakeContextCurrent(mpWindowHandle);
        int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        ORANGE_CORE_ASSERT(status, "Failed to initialize Glad!");
    }

    void OpenGLContext::SwapBuffers()
    {
        ORG_PROFILE_FUNCTION();

        glfwSwapBuffers(mpWindowHandle);
    }
}