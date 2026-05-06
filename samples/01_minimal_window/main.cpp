#include <orange/engine/app/AppHost.h>

#include <cstdio>

int main()
{
    using namespace Orange::Engine;

    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - Minimal Window";
    cfg.window.width  = 1280;
    cfg.window.height = 720;

    auto hostResult = AppHost::Create(cfg);
    if (hostResult.IsErr())
    {
        std::fprintf(stderr,
                     "AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostResult.Error()));
        return 1;
    }
    return hostResult.Value()->Run();
}
