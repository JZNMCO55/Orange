#ifndef ENTRYPOINT_H
#define ENTRYPOINT_H

#ifdef PLATFORM_WINDOWS

extern Orange::Application* Orange::CreateApplication();

int main(int argc, char** argv)
{
    spdlog::info("Orange Engine");
    auto app = Orange::CreateApplication();
    app->Run();
    delete app;
}

#endif // PLATFORM_WINDOWS

#endif // ENTRYPOINT_H