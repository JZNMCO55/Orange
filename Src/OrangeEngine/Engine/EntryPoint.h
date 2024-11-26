#ifndef ENTRYPOINT_H
#define ENTRYPOINT_H

#ifdef PLATFORM_WINDOWS

extern Orange::Application* Orange::CreateApplication();

int main(int argc, char** argv)
{
    std::cout << "Orange Engine" << std::endl;
    auto app = Orange::CreateApplication();
    app->Run();
    delete app;
}

#endif // PLATFORM_WINDOWS

#endif // ENTRYPOINT_H