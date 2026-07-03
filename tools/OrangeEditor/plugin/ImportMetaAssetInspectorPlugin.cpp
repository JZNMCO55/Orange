#include "ImportMetaAssetInspectorPlugin.h"

#include "../EditorHost.h"
#include "../import/MetaSidecar.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace Orange::Editor::Plugin
{

    namespace
    {
        // 已被其它 plugin 接管的扩展名一律 skip —— 避免 Material / AnimFsm /
        // DragonBones / Audio plugin 路径同时命中 .meta plugin（双接管）。当前
        // 这些 plugin 自家文件不走 importer，逻辑上也没 .meta；显式跳是防御
        // （日后某些 plugin 可能 import 时也写 .meta）。
        bool IsExtSkipped(const std::string& path)
        {
            auto ends = [&](const char* s)
            {
                const auto n = std::string_view(s).size();
                return path.size() >= n && path.compare(path.size() - n, n, s) == 0;
            };
            return ends(".material") || ends(".anim_fsm") || ends(".scene.json") || ends(".scene.manifest.json");
        }
    } // namespace

    bool ImportMetaAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
    {
        if (assetPath.empty() || IsExtSkipped(assetPath))
        {
            return false;
        }
        // 命中条件：同目录有 .meta sidecar 文件 —— 即"这是 importer 产物或
        // 任何被 .meta 标记的资产"。importer 三种产物（.mesh / .png-jpg-tga /
        // .hdr）自动覆盖。
        namespace fs = std::filesystem;
        std::error_code   ec;
        const std::string metaPath = ::Orange::Editor::Import::MetaPathFor(assetPath);
        return fs::exists(metaPath, ec) && !ec;
    }

    void ImportMetaAssetInspectorPlugin::Draw(EditorHost& host, const std::string& assetPath)
    {
        const std::string metaPath =
            ::Orange::Editor::Import::MetaPathFor(assetPath);
        auto meta = ::Orange::Editor::Import::ReadTextureMeta(metaPath);
        if (!meta.has_value())
        {
            ImGui::TextDisabled("(.meta read failed: %s)", metaPath.c_str());
            return;
        }

        ImGui::TextDisabled("Imported Asset");
        ImGui::Separator();

        ImGui::TextUnformatted("Asset Path");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", assetPath.c_str());

        ImGui::TextUnformatted("Source Path");
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            meta->sourcePath.empty()
                                ? "(not recorded)"
                                : meta->sourcePath.c_str());

        ImGui::TextUnformatted("Source Hash");
        ImGui::SameLine();
        const std::string hashHex =
            ::Orange::Editor::Import::HashToHexString(meta->sourceHash);
        ImGui::TextDisabled("%s", hashHex.c_str());

        ImGui::TextUnformatted("Handle Id");
        ImGui::SameLine();
        ImGui::TextDisabled("%llu",
                            static_cast<unsigned long long>(meta->handleId));

        ImGui::Spacing();
        // .meta v1 importParams 当前空 object；v1.2+ 加可写字段时此处替换为
        // 真实 ImGui 控件（normalmap green invert checkbox / scale slider 等）。
        ImGui::TextDisabled("Import Params: (v1 schema empty; "
                            "v1.2+ will add green-invert / scale / mipmap mode)");

        ImGui::Spacing();
        if (ImGui::Button("Reimport"))
        {
            if (!meta->sourcePath.empty())
            {
                host.pendingImports.push_back(meta->sourcePath);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(hash 短路：源文件未改 → log 'unchanged, skip')");
    }

} // namespace Orange::Editor::Plugin
