#include "MaterialAssetInspectorPlugin.h"

#include "../DemoWorld.h"
#include "../EditorHost.h"
#include "../EditorWidgets.h"
#include "../MaterialFileIO.h"

#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <imgui.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace Orange::Editor::Plugin
{

namespace
{

// 从 MaterialSystem 读所有已注册模板名（含内置 + 游戏侧 RegisterTemplate
// 注入的自定义模板），排序后返回——unordered_map 遍历无序，UI 一致
// 性要求按 name 字典序展示。
std::vector<std::string> CollectTemplateNames(
    const Orange::Engine::Render::MaterialSystem* pMaterials)
{
    if (pMaterials == nullptr) { return {}; }
    std::vector<std::string> names = pMaterials->GetTemplateNames();
    std::sort(names.begin(), names.end());
    return names;
}

// 读 .material 文件的 templateName 字段。失败 / templateName 缺失返回
// 空 string。底层走 MaterialFileIO 的 v1.1 reader（v1.0 兼容）。
std::string ReadMaterialTemplateName(const std::string& path)
{
    auto dataOpt = ::Orange::Editor::Material::ReadMaterialFile(path);
    if (!dataOpt.has_value()) { return {}; }
    return dataOpt->templateName;
}

// Material 子模式主体：读 .material 显示当前 templateName + Combo 切换 +
// uniform 调参 + Save 按钮。已加载的 MaterialInstance（通过
// host.assets.pMaterials 的 named instance map 反查）就地修改 uniform；
// Save 写回 .material 仅写 templateName（uniform 持久化 deferred）。
void DrawMaterialSubMode(EditorHost& host, const std::string& materialPath)
{
    ImGui::TextDisabled("Material:");
    ImGui::SameLine();
    ImGui::TextUnformatted(materialPath.c_str());
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", materialPath.c_str());
    }
    ImGui::Separator();

    // 读 .material 盘上原始 templateName（用于 dirty 判定）。
    const std::string originalTemplate = ReadMaterialTemplateName(materialPath);
    if (originalTemplate.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                           "无法读取 .material 文件 templateName 字段");
        return;
    }

    // 切到另一个 .material 文件 → 刷新 editing 缓存（首次进入或换文件）。
    // 否则 editingTemplateName 持续保留用户在 Combo 的选择，跨帧不丢——
    // 这是 B2 修复点：旧版每帧从盘重读 + 局部 newTemplateIdx 导致用户
    // 切 Combo 后下一帧立刻被盘上值覆盖，外观就是"Combo 切不动"。
    if (host.assets.editingMaterialPath != materialPath)
    {
        host.assets.editingMaterialPath = materialPath;
        host.assets.editingTemplateName = originalTemplate;
    }

    // 从 MaterialSystem 实时取所有已注册模板（含游戏侧自定义）。注：游戏侧
    // 若在 editor 启动后才注册，需要 host.assets.pMaterials 真实拿到那次注
    // 册结果；当前 host 单 session 内不动 RegisterTemplate，所以每帧重读
    // 即可——开销与每帧 ImGui 重布局同节奏，可忽略。
    const std::vector<std::string> templateNames =
        CollectTemplateNames(host.assets.pMaterials.get());

    // ImGui::Combo 需要 const char* 数组形式 —— 把 vector<string> 转成
    // vector<const char*> 喂给 Combo。
    std::vector<const char*> templateNameCStrs;
    templateNameCStrs.reserve(templateNames.size());
    for (const auto& n : templateNames) { templateNameCStrs.push_back(n.c_str()); }

    // 找当前 editingTemplateName 在列表里的 index（找不到走 -1 → Combo
    // 显示空）。当前文件 templateName 若是已被卸载的旧模板，Combo 显示
    // 空 + 用户可选切到任一已注册模板。
    int curTemplateIdx = -1;
    for (int i = 0; i < static_cast<int>(templateNames.size()); ++i)
    {
        if (host.assets.editingTemplateName == templateNames[i])
        {
            curTemplateIdx = i;
            break;
        }
    }
    Orange::Editor::Widgets::BeginPropertyTable("##matprops", 100.0f);
    Orange::Editor::Widgets::PropertyLabel("Template",
        "材质模板（决定 shader + uniform 布局）");
    if (!templateNameCStrs.empty()
        && ImGui::Combo("##template", &curTemplateIdx,
                        templateNameCStrs.data(),
                        static_cast<int>(templateNameCStrs.size())))
    {
        if (curTemplateIdx >= 0
            && curTemplateIdx < static_cast<int>(templateNames.size()))
        {
            host.assets.editingTemplateName = templateNames[curTemplateIdx];
        }
    }
    if (templateNameCStrs.empty())
    {
        ImGui::TextDisabled("(no templates registered)");
    }
    Orange::Editor::Widgets::EndPropertyTable();

    // 找到此 .material path 当前对应的运行时 MaterialInstance（如有）。
    // BuildNamedMaterialInstances 是 path → instance* 表的权威反查路径；
    // 当前 path 不在表里（demo 内置范围之外的 .material 文件）则 nullptr，
    // 走"无 live instance 时只能编辑磁盘 schema"的退化路径——本期 PBR 调
    // 参 UI 主要面向内置 pbr.material 一档，未命中时直接显示提示。
    auto namedMap = BuildNamedMaterialInstances(host.assets);
    auto namedIt = namedMap.find(materialPath);
    Orange::Engine::Render::MaterialInstance* liveInstance =
        (namedIt != namedMap.end()) ? namedIt->second : nullptr;

    // Uniform 调参 UI：当 template == "pbr" 且当前 .material 对应 live
    // MaterialInstance 找得到时展开 PBR 五通道调参（normal 通道走 vNormal
    // vertex 插值未引入 texture，本期面板不展示）。其他模板的 uniform 调
    // 参 UI 等后续 milestone 按需补；schema v1.1 已支持持久化全部 override。
    bool pbrUniformDirty = false;
    if (host.assets.editingTemplateName == "pbr" && liveInstance != nullptr)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("PBR 材质参数");

        // 当前 override 值 → fallback 到 Pipeline pack 路径里 PBR 默认
        // （灰塑料 + 非金属 + 中等粗糙 + AO 满）。任一字段在 .material
        // 文件里被持久化为 override 时本帧立即从 MaterialInstance 读回真值。
        glm::vec4 baseColor = liveInstance->GetUniformVec4("uBaseColor")
                                  .value_or(glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
        glm::vec4 mra       = liveInstance->GetUniformVec4("uMRA")
                                  .value_or(glm::vec4(0.0f, 0.5f, 1.0f, 0.0f));

        Orange::Editor::Widgets::BeginPropertyTable("##pbrprops", 100.0f);

        Orange::Editor::Widgets::PropertyLabel("Base Color",
            "线性 RGB 漫反射底色（金属时改变高光颜色，非金属时改变 diffuse）");
        float rgb[3] = {baseColor.r, baseColor.g, baseColor.b};
        if (ImGui::ColorEdit3("##baseColor", rgb))
        {
            liveInstance->SetUniform("uBaseColor",
                glm::vec4(rgb[0], rgb[1], rgb[2], baseColor.a));
            pbrUniformDirty = true;
        }

        Orange::Editor::Widgets::PropertyLabel("Metallic",
            "0 = 介电（塑料 / 木材），1 = 金属（高光取 baseColor，diffuse 趋 0）");
        if (ImGui::SliderFloat("##metallic", &mra.x, 0.0f, 1.0f, "%.3f"))
        {
            liveInstance->SetUniform("uMRA", mra);
            pbrUniformDirty = true;
        }

        Orange::Editor::Widgets::PropertyLabel("Roughness",
            "0 = 镜面，1 = 粗糙漫反射；shader 内 clamp 到 [0.04, 1.0] 避开 D_GGX 奇异");
        if (ImGui::SliderFloat("##roughness", &mra.y, 0.0f, 1.0f, "%.3f"))
        {
            liveInstance->SetUniform("uMRA", mra);
            pbrUniformDirty = true;
        }

        Orange::Editor::Widgets::PropertyLabel("AO",
            "环境光遮蔽乘子；仅作用于 IBL 贡献（当前 IBL 槽 dummy → 视觉不变）");
        if (ImGui::SliderFloat("##ao", &mra.z, 0.0f, 1.0f, "%.3f"))
        {
            liveInstance->SetUniform("uMRA", mra);
            pbrUniformDirty = true;
        }

        Orange::Editor::Widgets::EndPropertyTable();

        ImGui::TextDisabled("Normal: 当前走 vNormal vertex 插值（无法线贴图）；"
                            "tangent + 法线贴图基础设施落地后再上 texture 路径");
    }
    else if (host.assets.editingTemplateName == "pbr")
    {
        ImGui::Separator();
        ImGui::TextDisabled("PBR 五通道调参面板需要当前 .material 已被加载到运行时实例。");
        ImGui::TextDisabled("（本 .material 未出现在 namedMaterialInstances 表内，跳过）");
    }
    else
    {
        // 其他模板的 uniform / texture 调参 UI 等后续 milestone 按需扩；
        // schema v1.1 已支持持久化全部 SetUniform override。
        ImGui::Separator();
        ImGui::TextDisabled("Uniforms / Textures 调参 UI：仅 pbr 模板已上线。");
        ImGui::TextDisabled("(.material schema v1.1 已支持持久化所有 SetUniform override)");
    }

    // Save 按钮：以 v1.1 schema 写回。template 切换或任一 PBR uniform 编
    // 辑都标 dirty；live instance 不存在时仅按 template 差异判 dirty。
    ImGui::Separator();
    const bool templateDirty = (host.assets.editingTemplateName != originalTemplate);
    const bool dirty         = templateDirty || pbrUniformDirty;
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save"))
    {
        ::Orange::Editor::Material::MaterialFileData data;
        if (liveInstance != nullptr)
        {
            // 从运行时 instance 抽 override（含 PBR 编辑的 uBaseColor /
            // uMRA），再覆盖 templateName 字段——templateName 走用户在
            // Combo 的选择，而 instance 持有的 Material* 仍可能是旧模板
            // （Combo 切换 template 不会重建 instance，运行时直到重启才
            // 切换；本设计与 c5 阶段一致，避免悬挂 Renderable 指针）。
            data = ::Orange::Editor::Material::BuildDataFromInstance(
                *liveInstance, host.assets.editingTemplateName,
                host.assets.pAssets.get());
            data.templateName = host.assets.editingTemplateName;
        }
        else
        {
            data.templateName = host.assets.editingTemplateName;
        }
        ::Orange::Editor::Material::WriteMaterialFile(materialPath, data);
        // 内存 MaterialInstance 的 SetUniform override 已在编辑过程中
        // 应用到 live instance，视觉立即更新；磁盘 .material 文件本步
        // 落盘下次启动按 ApplyDataToInstance 重新加载相同的 override。
        ImGui::OpenPopup("##saved_notice");
    }
    ImGui::EndDisabled();
    if (!dirty)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(no changes to save)");
    }
    if (ImGui::BeginPopup("##saved_notice"))
    {
        ImGui::TextUnformatted("已保存到 .material 文件。");
        ImGui::Separator();
        if (templateDirty)
        {
            ImGui::TextDisabled("Template 切换：磁盘已写新模板名，运行时实例需要");
            ImGui::TextDisabled("重启 OrangeEditor 才会按新模板重建。");
        }
        else
        {
            ImGui::TextDisabled("Uniform 编辑：运行时实例已即时生效；磁盘 .material");
            ImGui::TextDisabled("文件 schema v1.1 已落盘下次启动 ApplyDataToInstance 还原。");
        }
        if (ImGui::Button("OK")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

}  // anonymous namespace

bool MaterialAssetInspectorPlugin::CanHandle(const std::string& assetPath) const
{
    // ".material" = 9 chars；短 path / 空 path 不命中。
    if (assetPath.size() < 9) { return false; }
    return assetPath.compare(assetPath.size() - 9, 9, ".material") == 0;
}

void MaterialAssetInspectorPlugin::Draw(EditorHost& host, const std::string& assetPath)
{
    DrawMaterialSubMode(host, assetPath);
}

}  // namespace Orange::Editor::Plugin
