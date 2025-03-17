#ifndef CONTENT_BROWSER_PANEL_H
#define CONTENT_BROWSER_PANEL_H

#include "Renderer/Texture.h"

namespace Orange
{
    class ContentBrowserPannel
    {
    public:
        ContentBrowserPannel();

        void OnImGuiRender();

        void SetDirectory(const std::filesystem::path& path);
    private:
        std::filesystem::path mCurrentDirectory;
        Ref<Texture2D> mpFolderIcon;
        Ref<Texture2D> mpFileIcon;
    };
}

#endif // CONTENT_BROWSER_PANEL_H