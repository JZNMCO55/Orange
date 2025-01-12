#ifndef ENTRYPOINT_H
#define ENTRYPOINT_H

#ifdef PLATFORM_WINDOWS

extern Orange::Application* Orange::CreateApplication();

int main(int argc, char** argv)
{
    Orange::Log::Init();

    ORG_PROFILE_BEGIN_SESSION("Startup", "OrangeProfile-Startup.json");
    auto app = Orange::CreateApplication();
    ORG_PROFILE_END_SESSION();
    ORG_PROFILE_BEGIN_SESSION("Runtime", "OrangeProfile-Runtime.json");
    app->Run();
    ORG_PROFILE_END_SESSION();
    ORG_PROFILE_BEGIN_SESSION("Startup", "OrangeProfile-Shutdown.json");
    delete app;
    ORG_PROFILE_END_SESSION();
}

#endif // PLATFORM_WINDOWS

#endif // ENTRYPOINT_H