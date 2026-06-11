#include "ScriptFieldOverridesInspectorPlugin.h"

#include "../EditorHost.h"
#include "../schema/ComponentSchema.h"

#include <orange/engine/script/ScriptComponent.h>

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace Orange::Editor::Plugin
{

namespace
{

// ScriptFieldType 枚举项名表 —— 顺序与枚举定义（Float=0 / Int=1 / Bool=2 /
// String=3）严格对齐，作 Combo 项列表。静态生命周期。
const char* const kFieldTypeNames[] = {"Float", "Int", "Bool", "String"};
constexpr int kFieldTypeCount =
    static_cast<int>(sizeof(kFieldTypeNames) / sizeof(kFieldTypeNames[0]));

static_assert(static_cast<int>(Orange::Engine::Script::ScriptFieldType::Float)  == 0,
              "ScriptFieldType enum drift");
static_assert(static_cast<int>(Orange::Engine::Script::ScriptFieldType::Int)    == 1,
              "ScriptFieldType enum drift");
static_assert(static_cast<int>(Orange::Engine::Script::ScriptFieldType::Bool)   == 2,
              "ScriptFieldType enum drift");
static_assert(static_cast<int>(Orange::Engine::Script::ScriptFieldType::String) == 3,
              "ScriptFieldType enum drift");

// 把 std::string 拷进定长 buffer 给 ImGui::InputText 用（与 SchemaInspector
// String case 同款，避免 imgui_stdlib 依赖）。返回是否被编辑、新值写回 out。
bool InputTextField(const char* id, std::string& out, std::size_t bufSize = 256)
{
    char buffer[256] = {};
    const std::size_t cap = std::min<std::size_t>(bufSize, sizeof(buffer));
    const std::size_t copyN = std::min<std::size_t>(out.size(), cap - 1);
    std::memcpy(buffer, out.data(), copyN);
    if (ImGui::InputText(id, buffer, cap))
    {
        out.assign(buffer);
        return true;
    }
    return false;
}

}  // namespace

bool ScriptFieldOverridesInspectorPlugin::CanHandle(
    const Orange::Editor::Schema::ComponentSchema& schema) const
{
    if (schema.typeName == nullptr) { return false; }
    return std::string_view(schema.typeName) == std::string_view("Script");
}

void ScriptFieldOverridesInspectorPlugin::ParseEnd(
    EditorHost&                                            host,
    Orange::Engine::Entity                                 entity,
    const Orange::Editor::Schema::ComponentSchema&         schema,
    void*                                                  component)
{
    (void)host;
    (void)entity;
    (void)schema;

    using SC = Orange::Engine::Script::ScriptComponent;
    using FO = Orange::Engine::Script::ScriptFieldOverride;
    using FT = Orange::Engine::Script::ScriptFieldType;

    auto* pSc = static_cast<SC*>(component);
    if (pSc == nullptr) { return; }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Field Overrides");
    ImGui::SameLine();
    ImGui::TextDisabled("(authored 值，实例化后 OnStart 前注入脚本 public 字段)");

    // 删除请求延后到本帧渲染完再 erase（避免遍历中改容器使迭代器失效）。
    // -1 = 无删除请求。
    int removeIndex = -1;

    for (std::size_t i = 0; i < pSc->fieldOverrides.size(); ++i)
    {
        FO& ov = pSc->fieldOverrides[i];

        // 每行用 index 作 ImGui ID scope，避免同 label 控件 ID 冲突。
        ImGui::PushID(static_cast<int>(i));

        // [name] [type combo] [value] [x 删除] 单行布局。整行可用宽分摊给
        // 三个编辑控件；删除按钮固定窄。
        const float removeBtnWidth = ImGui::GetFrameHeight();
        const float spacing        = ImGui::GetStyle().ItemSpacing.x;
        const float avail          = ImGui::GetContentRegionAvail().x;
        // 三控件等分剩余宽（扣掉删除按钮 + 三段间距）。
        const float fieldWidth =
            std::max(40.0f, (avail - removeBtnWidth - spacing * 3.0f) / 3.0f);

        // name（string）
        ImGui::SetNextItemWidth(fieldWidth);
        InputTextField("##name", ov.name);

        // type（enum 下拉）
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fieldWidth);
        int typeIdx = static_cast<int>(ov.type);
        if (typeIdx < 0 || typeIdx >= kFieldTypeCount) { typeIdx = 0; }
        if (ImGui::Combo("##type", &typeIdx, kFieldTypeNames, kFieldTypeCount))
        {
            ov.type = static_cast<FT>(typeIdx);
        }

        // value（string —— 统一字符串形态，与 ScriptFieldOverride::value 语义
        // 一致：Float→"2.5" / Int→"3" / Bool→"true"/"false" / String→原文。
        // 运行期在 C# 边界按 type 解析，此处不做类型化控件，保持 ABI 最简）。
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fieldWidth);
        InputTextField("##value", ov.value);

        // 删除本行
        ImGui::SameLine();
        if (ImGui::Button("x##remove", ImVec2(removeBtnWidth, 0.0f)))
        {
            removeIndex = static_cast<int>(i);
        }

        ImGui::PopID();
    }

    if (removeIndex >= 0 &&
        static_cast<std::size_t>(removeIndex) < pSc->fieldOverrides.size())
    {
        pSc->fieldOverrides.erase(pSc->fieldOverrides.begin() + removeIndex);
    }

    // 末尾追加一条新 override（默认 Float / 空 name / 空 value）。
    if (ImGui::Button("+ Add Override##script_field_override"))
    {
        FO ov;
        ov.type = FT::Float;
        pSc->fieldOverrides.push_back(std::move(ov));
    }
}

}  // namespace Orange::Editor::Plugin
