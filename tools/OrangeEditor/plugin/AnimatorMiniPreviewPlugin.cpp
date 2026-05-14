#include "AnimatorMiniPreviewPlugin.h"

#include "../schema/ComponentSchema.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/IAnimator.h>

#include <imgui.h>

#include <string_view>

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
    (void)host;
    (void)entity;
    (void)schema;

    using AC = Orange::Engine::Animation::AnimatorComponent;
    auto* pAc = static_cast<AC*>(component);
    if (pAc == nullptr) { return; }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Mini-Preview (IEditorInspectorPlugin demo)");

    if (!pAc->animator)
    {
        // 没挂 backend：schema 默认段已经在 "backend" 字段显示 "(no backend)"，
        // 这里仅追加一条灰色状态说明，与 schema 段语义一致。
        ImGui::TextDisabled("Status: (no backend attached)");
        return;
    }

    const bool finished = pAc->animator->IsFinished();
    const ImVec4 statusColor = finished
        ? ImVec4(0.65f, 0.65f, 0.65f, 1.0f)   // finished = 灰
        : ImVec4(0.40f, 0.90f, 0.40f, 1.0f);  // running  = 绿
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
