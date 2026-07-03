#include "DragonBonesAssetInspectorPlugin.h"

#include "../EditorHost.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/SkeletonAsset.h>

#include <imgui.h>

#include <cstring>

namespace Orange::Editor::Plugin
{

    namespace
    {

        bool EndsWith(const std::string& path, std::string_view suffix)
        {
            if (path.size() < suffix.size())
            {
                return false;
            }
            return path.compare(path.size() - suffix.size(),
                                suffix.size(), suffix) == 0;
        }

    } // anonymous namespace

    DragonBonesAssetInspectorPlugin::DragonBonesAssetInspectorPlugin()  = default;
    DragonBonesAssetInspectorPlugin::~DragonBonesAssetInspectorPlugin() = default;

    bool DragonBonesAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
    {
        // DragonBones 标准 skeleton 资源后缀：_ske.json / _ske.dbbin
        return EndsWith(assetPath, "_ske.json") || EndsWith(assetPath, "_ske.dbbin");
    }

    void DragonBonesAssetInspectorPlugin::Draw(EditorHost&        host,
                                               const std::string& assetPath)
    {
        // 路径变化 → 重 Load；缓存避免每帧重 Load 浪费
        if (mCachedPath != assetPath)
        {
            mCachedPath = assetPath;
            mpAsset.reset();
            mLoadAttempted = false;
            mLoadFailed    = false;
        }

        ImGui::TextDisabled("DragonBones Skeleton:");
        ImGui::SameLine();
        ImGui::TextUnformatted(assetPath.c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", assetPath.c_str());
        }
        ImGui::Separator();

        // lazy load
        if (!mLoadAttempted)
        {
            mLoadAttempted = true;
            if (!host.assets.pAssets)
            {
                mLoadFailed = true;
            }
            else
            {
                auto lr = host.assets.pAssets->Load<::Orange::Engine::Asset::SkeletonAsset>(assetPath);
                if (lr.IsErr())
                {
                    mLoadFailed = true;
                }
                else
                {
                    // AssetRegistry::Load 返回 AssetHandle —— 通过 GetAsset 拿
                    // 实际 SkeletonAsset const*；plugin 缓存一个 owning copy
                    // 以避免 handle 失效（编辑器不会 unload，但模式保持稳健）
                    const auto* p = host.assets.pAssets->Get(lr.Value());
                    if (p == nullptr || p->Empty())
                    {
                        mLoadFailed = true;
                    }
                    else
                    {
                        mpAsset = std::make_unique<::Orange::Engine::Asset::SkeletonAsset>(
                            std::string(p->DragonBonesName()),
                            std::vector<::Orange::Engine::Asset::ArmatureMeta>(
                                p->Armatures().begin(),
                                p->Armatures().end()));
                    }
                }
            }
        }

        if (mLoadFailed || !mpAsset)
        {
            ImGui::TextColored(Orange::Editor::Theme::Color::GetAlertError(),
                               "无法读取 DragonBones 资源（详见 stderr）");
            ImGui::TextDisabled("可能原因：");
            ImGui::TextDisabled(" - SkeletonLoader 未注册（DemoWorld 启动期应注册）");
            ImGui::TextDisabled(" - 文件格式错误 / .dbbin binary 路径暂未实现（reader 端 SchemaMismatch）");
            return;
        }

        // 元数据展示
        ImGui::Text("DragonBones name: %s", std::string(mpAsset->DragonBonesName()).c_str());
        const auto armatures = mpAsset->Armatures();
        ImGui::Text("Armatures:        %zu", armatures.size());
        ImGui::Separator();

        for (std::size_t i = 0; i < armatures.size(); ++i)
        {
            const auto& a = armatures[i];
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNodeEx(a.name.c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Bones (%zu):", a.boneNames.size());
                if (ImGui::BeginTable("##bones", 1,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                      ImVec2(0.0f, ImGui::GetFontSize() * 6.0f)))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableHeadersRow();
                    for (const auto& n : a.boneNames)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(n.c_str());
                    }
                    ImGui::EndTable();
                }

                ImGui::Text("Animations (%zu):", a.animationNames.size());
                for (std::size_t k = 0; k < a.animationNames.size(); ++k)
                {
                    const auto& n = a.animationNames[k];
                    ImGui::PushID(static_cast<int>(k));
                    ImGui::BulletText("%s", n.c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Preview##anim"))
                    {
                        // c3 scope：单 clip 预览在编辑器 viewport 内渲染需要独立
                        // SkeletalAnimator 实例 + preview viewport，工程量超 c3。
                        // 当前仅 toast 提示，留 c3 patch / v1.x。
                        ImGui::OpenPopup("##preview_todo");
                    }
                    ImGui::PopID();
                }

                if (ImGui::BeginPopup("##preview_todo"))
                {
                    ImGui::TextUnformatted("单 clip 预览（viewport 渲染）留 v1.x");
                    ImGui::TextDisabled("当前 c3 scope：仅 metadata 浏览");
                    if (ImGui::Button("OK"))
                    {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::TextDisabled("c3 scope：DragonBones skeleton metadata 浏览");
        ImGui::TextDisabled("单 clip 实时预览渲染留 c3 patch / v1.x（需独立 preview viewport）");
    }

} // namespace Orange::Editor::Plugin
