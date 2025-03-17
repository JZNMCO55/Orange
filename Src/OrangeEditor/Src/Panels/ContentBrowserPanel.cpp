#include "pch.h"
#include "ContentBrowserPanel.h"
#include "imgui/imgui.h"

namespace Orange
{
    static std::filesystem::path sAssetDirectory = "../../Resource";

    ContentBrowserPannel::ContentBrowserPannel()
        : mCurrentDirectory(sAssetDirectory)
    {
        mpFileIcon = Texture2D::Create(R"(../../Resource/Icons/FileIcon.png)");
        mpFolderIcon = Texture2D::Create(R"(../../Resource/Icons/FoldIcon.png)");
    }

    void ContentBrowserPannel::OnImGuiRender()
    {
        ImGui::Begin("Content Browser");

        if (mCurrentDirectory != std::filesystem::path(sAssetDirectory))
        {
            if (ImGui::Button("<-"))
            {
                mCurrentDirectory = mCurrentDirectory.parent_path();
            }
        }

        static float padding = 16.0f;
        static float thumbnailSize = 128.0f;
        float cellSize = thumbnailSize + padding;

        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize);
        if (columnCount < 1)
        {
            columnCount = 1;
        }

        ImGui::Columns(columnCount, 0, false);

        for (auto& dirctoryEntry : std::filesystem::directory_iterator(mCurrentDirectory))
        {
            const auto& path = dirctoryEntry.path();
            auto relativPath = std::filesystem::relative(path, sAssetDirectory);
            std::string fileName = relativPath.filename().string();

            Ref<Texture2D> tpIcon = dirctoryEntry.is_directory()? mpFolderIcon : mpFileIcon;
            ImGui::ImageButton(fileName.c_str(), (ImTextureID)tpIcon->GetRendererID(),
                { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (dirctoryEntry.is_directory())
                {
                    mCurrentDirectory /= path.filename();
                }
            }
            ImGui::TextWrapped(fileName.c_str());

            ImGui::NextColumn();
        }

        ImGui::Columns(1);

        ImGui::SliderFloat("Thumbnail Size", &thumbnailSize, 16, 512);
        ImGui::SliderFloat("Padding", &padding, 0, 32);

        ImGui::End();
    }

    void ContentBrowserPannel::SetDirectory(const std::filesystem::path& path)
    {
        mCurrentDirectory = path;
    }
}