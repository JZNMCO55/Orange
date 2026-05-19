#include "AnimatorMiniPreviewPlugin.h"

#include "../EditorHost.h"
#include "../command/EntityCommands.h"
#include "../context/EditorSceneContext.h"
#include "../schema/ComponentSchema.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/IAnimator.h>

#include <imgui.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Editor::Plugin
{

bool AnimatorMiniPreviewPlugin::CanHandle(
    const Orange::Editor::Schema::ComponentSchema& schema) const
{
    // typeName 来自 ComponentSchemaBuilder<AC>("Animator", "Animator")—— 第一
    // 个参数 = typeName。null 防御按头注释推荐："按 typeName 字符串比较"。
    if (schema.typeName == nullptr) { return false; }
    return std::string_view(schema.typeName) == std::string_view("Animator");
}

void AnimatorMiniPreviewPlugin::ParseEnd(
    EditorHost&                                            host,
    Orange::Engine::Entity                                 entity,
    const Orange::Editor::Schema::ComponentSchema&         schema,
    void*                                                  component)
{
    (void)schema;

    using AC = Orange::Engine::Animation::AnimatorComponent;
    auto* pAc = static_cast<AC*>(component);
    if (pAc == nullptr) { return; }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Mini-Preview (IEditorInspectorPlugin demo)");

    // v0.7 c1：Backend 切换 Combo —— 即便当前 animator 是 null 也能 Combo
    // 选个 backend 让 AnimatorComponent 真正挂上 backend，因此 Combo 段必
    // 须在 null guard 之上独立渲染。Mini-Preview 状态行依然保留 null guard
    // 的早退路径（无 backend 没 Status / Backend 信息可读）。
    auto* pRegistry = host.assets.pAnimators.get();
    if (pRegistry != nullptr)
    {
        // 每帧从 registry 实时枚举可用 backend，让游戏侧动态 Register/Unregister
        // 即时反映；BackendNames 内部已按字典序排序保证 Combo 顺序稳定。
        const std::vector<std::string> backendNames = pRegistry->BackendNames();

        // 当前 backend 名作为 Combo 显示值；animator==nullptr 时显示 (none)。
        const std::string currentName = pAc->animator
            ? std::string(pAc->animator->BackendName())
            : std::string{};

        // 找 currentName 在列表中的 index（找不到走 -1 → Combo 显示空）。
        // 找不到的情况：游戏侧外部注册过 backend、用 Register 切到该 backend、
        // 后又 Unregister；当前 animator 还活着但 registry 不认得它了。
        int curIdx = -1;
        for (int i = 0; i < static_cast<int>(backendNames.size()); ++i)
        {
            if (backendNames[static_cast<std::size_t>(i)] == currentName)
            {
                curIdx = i;
                break;
            }
        }

        // ImGui::Combo 需要 const char* 数组 —— 转一次。
        std::vector<const char*> backendNameCStrs;
        backendNameCStrs.reserve(backendNames.size());
        for (const auto& n : backendNames) { backendNameCStrs.push_back(n.c_str()); }

        // Play / Paused 期间禁用 Combo —— 与 Inspector 主路径 Play 期 read-only
        // 行为一致；切换 backend 在 runtime 期有损，仅 Edit 期允许。
        const bool canEdit = (host.scene.playState == PlayState::Edit);
        ImGui::BeginDisabled(!canEdit);
        if (!backendNameCStrs.empty()
            && ImGui::Combo("Backend##animator_switch", &curIdx,
                            backendNameCStrs.data(),
                            static_cast<int>(backendNameCStrs.size())))
        {
            if (curIdx >= 0
                && curIdx < static_cast<int>(backendNames.size()))
            {
                const std::string& chosen = backendNames[static_cast<std::size_t>(curIdx)];
                if (chosen != currentName)
                {
                    host.cmdStack.Push(std::make_unique<SwitchAnimatorBackendCommand>(
                        host, entity, currentName, chosen));
                }
            }
        }
        if (backendNameCStrs.empty())
        {
            ImGui::TextDisabled("(no animator backends registered)");
        }
        ImGui::EndDisabled();
    }

    // 状态 / Backend 显示行 —— 依赖 animator 非空；与 v0.3 c5 行为一致。
    if (!pAc->animator)
    {
        ImGui::TextDisabled("Status: (no backend attached)");
        return;
    }

    // v0.6.5 c7：状态色用 EditorTheme token 替换字面量 RGBA（与 lint
    // editor-literal-rgba 规则对齐）。finished = TextDisabled 灰；running
    // = AlertSuccess 绿，语义对应 alert 系列"就绪 / 成功"状态。
    const bool finished = pAc->animator->IsFinished();
    const ImVec4 statusColor = finished
        ? Orange::Editor::Theme::Color::GetTextDisabled()
        : Orange::Editor::Theme::Color::GetAlertSuccess();
    ImGui::TextColored(statusColor,
                       "Status: %s", finished ? "Finished" : "Running");

    // backend name 复述一行（虽然 schema 默认段已显示，这里给 plugin 自身
    // "可读出 IAnimator API"的视觉证据——证明 plugin 不只是 ImGui 装饰，
    // 确实能消费 component 的运行时数据）。
    const auto backendName = pAc->animator->BackendName();
    ImGui::TextDisabled("Backend: %.*s",
                        static_cast<int>(backendName.size()),
                        backendName.data());
}

}  // namespace Orange::Editor::Plugin
