#include "MaterialAssetInspectorPlugin.h"

#include "../BuiltinAssets.h"  // BuildNamedMaterialInstances（v1.0.1 c11 拆出）
#include "../EditorHost.h"
#include "../EditorWidgets.h"
#include "../MaterialFileIO.h"
#include "../ShaderTemplateMetaIO.h"  // v1.2 T2 · 数据驱动 widget 元数据

#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Orange::Editor::Plugin
{

namespace
{

// v1.2 T2 · 所有已注册 .template.json 的 editor 元数据缓存。启动期由
// EnsureMetaCache 首次调用时一次性 LoadAllShaderTemplateMetas 填好；后续
// 每帧渲染按 templateName 线性查找（典型 ≤15 个 template，O(N) 可忽略）。
// 用户加新 .template.json 后需重启编辑器才生效（与 MaterialSystem 注册
// 路径同节奏，不引入 file watcher）。
std::vector<ShaderMeta::ShaderTemplateMeta> sMetaCache;
bool sMetaCacheInitialized = false;

void EnsureMetaCache()
{
    if (sMetaCacheInitialized) { return; }
    sMetaCache = ShaderMeta::LoadAllShaderTemplateMetas(
        std::filesystem::path("assets/shaders/templates"));
    sMetaCacheInitialized = true;
}

const ShaderMeta::ShaderTemplateMeta* FindMeta(std::string_view templateName)
{
    EnsureMetaCache();
    for (const auto& m : sMetaCache)
    {
        if (m.templateName == templateName) { return &m; }
    }
    return nullptr;
}

// v1.2 T2 · 单 uniform widget 渲染。按 metadata.widget + uniform.type 派
// 发对应 ImGui 控件。返回 true 表示用户本帧改了值（外层 dirty 触发 Save）。
//
// 调用约定：调用方处于 BeginPropertyTable / EndPropertyTable 之间（uniform
// 占两列：左列 PropertyLabel，右列 widget）。Components 模式打破表格——
// 内部 End 当前 table + 给每 sub component 独立 row + 调用方退回前重
// Begin。
bool RenderUniformWidget(
    const ShaderMeta::UniformMetadata&            u,
    ::Orange::Engine::Render::MaterialInstance&   instance)
{
    using ::Orange::Engine::Render::MaterialUniformType;
    using W = ShaderMeta::UniformWidget;

    const std::string idStr = "##um_" + u.name;
    bool changed = false;

    switch (u.type)
    {
        case MaterialUniformType::Vec4:
        {
            const glm::vec4 fallback =
                u.hasDefault
                    ? glm::vec4(u.defaultValue[0], u.defaultValue[1],
                                u.defaultValue[2], u.defaultValue[3])
                    : glm::vec4(0.0f);
            glm::vec4 v = instance.GetUniformVec4(u.name).value_or(fallback);
            if (u.widget == W::Components)
            {
                // 打破当前 table，让每 sub component 各占一 row。外层
                // displayName 不渲染——与原 pbr hardcode "Metallic /
                // Roughness / AO" 三行直排视觉一致。
                Orange::Editor::Widgets::EndPropertyTable();
                Orange::Editor::Widgets::BeginPropertyTable(
                    ("##matparams_comp_" + u.name).c_str(), 100.0f);
                float arr[4] = {v.x, v.y, v.z, v.w};
                for (std::size_t i = 0;
                     i < u.components.size() && i < 4; ++i)
                {
                    const auto& c = u.components[i];
                    if (c.widget == W::Hidden) { continue; }
                    Orange::Editor::Widgets::PropertyLabel(
                        c.label.c_str(),
                        c.tooltip.empty() ? nullptr : c.tooltip.c_str());
                    const std::string subId =
                        idStr + "_" + std::to_string(i);
                    const float lo =
                        c.range.has_value() ? c.range->first  : 0.0f;
                    const float hi =
                        c.range.has_value() ? c.range->second : 1.0f;
                    const float step = c.step.value_or(0.001f);
                    bool subChanged = false;
                    if (c.widget == W::Slider)
                    {
                        subChanged = ImGui::SliderFloat(
                            subId.c_str(), &arr[i], lo, hi, "%.3f");
                    }
                    else
                    {
                        subChanged = ImGui::DragFloat(
                            subId.c_str(), &arr[i], step, lo, hi, "%.3f");
                    }
                    if (subChanged) { changed = true; }
                }
                Orange::Editor::Widgets::EndPropertyTable();
                Orange::Editor::Widgets::BeginPropertyTable(
                    "##matparams", 100.0f);
                if (changed)
                {
                    instance.SetUniform(
                        u.name,
                        glm::vec4(arr[0], arr[1], arr[2], arr[3]));
                }
                return changed;
            }
            // 非 Components 模式 —— 走外层 PropertyLabel + 右列 widget。
            const char* label = u.displayName.empty()
                                    ? u.name.c_str()
                                    : u.displayName.c_str();
            Orange::Editor::Widgets::PropertyLabel(
                label, u.tooltip.empty() ? nullptr : u.tooltip.c_str());
            if (u.widget == W::Color)
            {
                float arr[4] = {v.r, v.g, v.b, v.a};
                if (ImGui::ColorEdit4(idStr.c_str(), arr))
                {
                    instance.SetUniform(
                        u.name,
                        glm::vec4(arr[0], arr[1], arr[2], arr[3]));
                    changed = true;
                }
            }
            else
            {
                float arr[4] = {v.x, v.y, v.z, v.w};
                const float step = u.step.value_or(0.01f);
                if (ImGui::DragFloat4(idStr.c_str(), arr, step))
                {
                    instance.SetUniform(
                        u.name,
                        glm::vec4(arr[0], arr[1], arr[2], arr[3]));
                    changed = true;
                }
            }
            break;
        }
        case MaterialUniformType::Vec3:
        {
            const glm::vec3 fallback =
                u.hasDefault
                    ? glm::vec3(u.defaultValue[0], u.defaultValue[1],
                                u.defaultValue[2])
                    : glm::vec3(0.0f);
            glm::vec3 v = instance.GetUniformVec3(u.name).value_or(fallback);
            const char* label = u.displayName.empty()
                                    ? u.name.c_str()
                                    : u.displayName.c_str();
            Orange::Editor::Widgets::PropertyLabel(
                label, u.tooltip.empty() ? nullptr : u.tooltip.c_str());
            float arr[3] = {v.x, v.y, v.z};
            if (u.widget == W::Color)
            {
                if (ImGui::ColorEdit3(idStr.c_str(), arr))
                {
                    instance.SetUniform(
                        u.name, glm::vec3(arr[0], arr[1], arr[2]));
                    changed = true;
                }
            }
            else
            {
                const float step = u.step.value_or(0.01f);
                if (ImGui::DragFloat3(idStr.c_str(), arr, step))
                {
                    instance.SetUniform(
                        u.name, glm::vec3(arr[0], arr[1], arr[2]));
                    changed = true;
                }
            }
            break;
        }
        case MaterialUniformType::Vec2:
        {
            const glm::vec2 fallback =
                u.hasDefault
                    ? glm::vec2(u.defaultValue[0], u.defaultValue[1])
                    : glm::vec2(0.0f);
            glm::vec2 v = instance.GetUniformVec2(u.name).value_or(fallback);
            const char* label = u.displayName.empty()
                                    ? u.name.c_str()
                                    : u.displayName.c_str();
            Orange::Editor::Widgets::PropertyLabel(
                label, u.tooltip.empty() ? nullptr : u.tooltip.c_str());
            float arr[2] = {v.x, v.y};
            const float step = u.step.value_or(0.01f);
            if (ImGui::DragFloat2(idStr.c_str(), arr, step))
            {
                instance.SetUniform(u.name, glm::vec2(arr[0], arr[1]));
                changed = true;
            }
            break;
        }
        case MaterialUniformType::Float:
        {
            const float fallback = u.hasDefault ? u.defaultValue[0] : 0.0f;
            float v = instance.GetUniformFloat(u.name).value_or(fallback);
            const char* label = u.displayName.empty()
                                    ? u.name.c_str()
                                    : u.displayName.c_str();
            Orange::Editor::Widgets::PropertyLabel(
                label, u.tooltip.empty() ? nullptr : u.tooltip.c_str());
            const float lo = u.range.has_value() ? u.range->first  : 0.0f;
            const float hi = u.range.has_value() ? u.range->second : 1.0f;
            const float step = u.step.value_or(0.01f);
            if (u.widget == W::Slider)
            {
                if (ImGui::SliderFloat(idStr.c_str(), &v, lo, hi, "%.3f"))
                {
                    instance.SetUniform(u.name, v);
                    changed = true;
                }
            }
            else
            {
                if (ImGui::DragFloat(idStr.c_str(), &v, step,
                                     lo, hi, "%.3f"))
                {
                    instance.SetUniform(u.name, v);
                    changed = true;
                }
            }
            break;
        }
        case MaterialUniformType::Int:
        {
            const std::int32_t fallback =
                u.hasDefault ? static_cast<std::int32_t>(u.defaultValue[0]) : 0;
            std::int32_t v = instance.GetUniformInt(u.name).value_or(fallback);
            const char* label = u.displayName.empty()
                                    ? u.name.c_str()
                                    : u.displayName.c_str();
            Orange::Editor::Widgets::PropertyLabel(
                label, u.tooltip.empty() ? nullptr : u.tooltip.c_str());
            if (ImGui::InputInt(idStr.c_str(), &v))
            {
                instance.SetUniform(u.name, v);
                changed = true;
            }
            break;
        }
        case MaterialUniformType::Mat4:
            // mat4 不暴露——Pipeline 自动 push uMVP / uModel，schema 内
            // widget=Hidden 应该在外层就被跳过；走到这里是 schema 配错，
            // 静默忽略不渲染。
            break;
    }
    return changed;
}

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

    // v1.2 T2 · 数据驱动 widget 渲染。从 .template.json 元数据生成对应
    // 控件，移除原 pbr hardcode if-else 路径。Material Inspector 现在
    // 支持所有 6 个 baseline template + 未来用户自定义 .template.json
    // 自动出 UI；新增模板调参字段 = 改 .template.json 不动 C++。
    //
    // 渲染流程：FindMeta 按 editingTemplateName 查 metadata → 跳过全
    // Hidden 字段后渲染剩余 widget（PropertyTable 两列布局，左列
    // PropertyLabel + 右列 ImGui control）。SetUniform → live instance
    // 视觉立即生效语义保留。
    bool uniformDirty = false;
    if (liveInstance == nullptr)
    {
        ImGui::Separator();
        ImGui::TextDisabled("调参面板需要当前 .material 已被加载到运行时实例。");
        ImGui::TextDisabled("(本 .material 未出现在 namedMaterialInstances 表内，跳过)");
    }
    else
    {
        const ShaderMeta::ShaderTemplateMeta* meta =
            FindMeta(host.assets.editingTemplateName);
        if (meta == nullptr)
        {
            ImGui::Separator();
            ImGui::TextDisabled(
                "Uniforms：模板 '%s' 缺失 .template.json 元数据。",
                host.assets.editingTemplateName.c_str());
        }
        else
        {
            std::size_t visibleCount = 0;
            for (const auto& u : meta->uniforms)
            {
                if (u.widget != ShaderMeta::UniformWidget::Hidden)
                {
                    ++visibleCount;
                }
            }
            if (visibleCount == 0)
            {
                ImGui::Separator();
                ImGui::TextDisabled(
                    "模板 '%s' 无可调参数（uniform 全为 Pipeline 自动 push）。",
                    host.assets.editingTemplateName.c_str());
            }
            else
            {
                ImGui::Separator();
                ImGui::TextUnformatted("材质参数");
                Orange::Editor::Widgets::BeginPropertyTable(
                    "##matparams", 100.0f);
                for (const auto& u : meta->uniforms)
                {
                    if (u.widget == ShaderMeta::UniformWidget::Hidden)
                    {
                        continue;
                    }
                    if (RenderUniformWidget(u, *liveInstance))
                    {
                        uniformDirty = true;
                    }
                }
                Orange::Editor::Widgets::EndPropertyTable();
            }
        }
    }

    // Save 按钮：以 v1.1 schema 写回。template 切换或任一 uniform 编辑
    // 都标 dirty；live instance 不存在时仅按 template 差异判 dirty。
    ImGui::Separator();
    const bool templateDirty = (host.assets.editingTemplateName != originalTemplate);
    const bool dirty         = templateDirty || uniformDirty;
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
