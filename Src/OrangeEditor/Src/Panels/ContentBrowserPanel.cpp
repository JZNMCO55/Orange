#include "pch.h"
#include "ContentBrowserPanel.h"
#include "imgui/imgui.h"

namespace Orange
{
    extern std::filesystem::path gAssetDirectory = "../../Resource";

    ContentBrowserPannel::ContentBrowserPannel()
        : mCurrentDirectory(gAssetDirectory)
    {
        mpFileIcon = Texture2D::Create(R"(../../Resource/Icons/FileIcon.png)");
        mpFolderIcon = Texture2D::Create(R"(../../Resource/Icons/FoldIcon.png)");
    }

    void ContentBrowserPannel::OnImGuiRender()
    {
        ImGui::Begin("Content Browser");

        if (mCurrentDirectory != std::filesystem::path(gAssetDirectory))
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
            auto relativPath = std::filesystem::relative(path, gAssetDirectory);
            std::string fileName = relativPath.filename().string();

            ImGui::PushID(fileName.c_str());
            Ref<Texture2D> tpIcon = dirctoryEntry.is_directory()? mpFolderIcon : mpFileIcon;
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::ImageButton(fileName.c_str(), (ImTextureID)tpIcon->GetRendererID(),
                { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });
            
            if (ImGui::BeginDragDropSource())
            {
                const wchar_t* itemPath = relativPath.c_str();
                ImGui::SetDragDropPayload("CONTENT_BROWSER_ITEM", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
                ImGui::EndDragDropSource();
            }

            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (dirctoryEntry.is_directory())
                {
                    mCurrentDirectory /= path.filename();
                }
            }
            ImGui::TextWrapped(fileName.c_str());

            ImGui::NextColumn();
            ImGui::PopID();
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