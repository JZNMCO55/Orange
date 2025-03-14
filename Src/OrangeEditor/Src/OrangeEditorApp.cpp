#include "Orange.h"
#include "EntryPoint.h"

#include "EditorLayer.h"

namespace Orange
{
    class OrangeEditor : public Application
    {
    public:
        OrangeEditor(ApplicationCommandLineArgs args)
            : Application("Orange Editor", args)
        {
            auto tpLayer = CreateRef<EditorLayer>();
            PushLayer(tpLayer);
        }

        ~OrangeEditor() = default;
    };

    Application* CreateApplication(ApplicationCommandLineArgs args)
    {
        return new OrangeEditor(args);
    }
}