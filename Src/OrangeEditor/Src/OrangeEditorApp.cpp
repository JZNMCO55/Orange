#include "Orange.h"
#include "EntryPoint.h"

#include "EditorLayer.h"

namespace Orange
{
    class OrangeEditor : public Application
    {
    public:
        OrangeEditor()
            : Application("Orange Editor")
        {
            auto tpLayer = CreateRef<EditorLayer>();
            PushLayer(tpLayer);
        }

        ~OrangeEditor() = default;
    };

    Application* CreateApplication()
    {
        return new OrangeEditor();
    }
}