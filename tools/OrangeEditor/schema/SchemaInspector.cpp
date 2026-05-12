// SchemaInspector 实现 —— 见 SchemaInspector.h 设计说明。
//
// 设计要点：
//   * DrawProperty 对 PropertyType 走 switch；每个 case 走"读 old → 出
//     ImGui 控件 → 检测 changed → set new + Push SetFieldValueCommand"
//     标准模板
//   * SetFieldValueCommand 的 ApplyFn 闭包内通过 schema.get 重新拿 component
//     指针（每次 Undo/Redo 都重新查询，因为 component 可能被 RemoveComponent
//     再 AddComponent 重新生成新地址）
//   * fieldKey 拼接为 "{schema.typeName}.{prop.name}" 字符串字面量组合，
//     与现有 SetFieldValueCommand fieldKey 风格一致（"transform.position"
//     等），确保 coalesce 正确

#include "SchemaInspector.h"

#include "../EditorHost.h"
#include "../EditorWidgets.h"
#include "../command/SetFieldValueCommand.h"
#include "ComponentSchemaRegistry.h"

#include <orange/engine/scene/World.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <imgui.h>

#include <memory>
#include <string>

namespace Orange::Editor::Schema
{

namespace
{

// ComponentHeader 的本地副本（与 EditorRenderLayer::ComponentHeader 同
// 语义）—— schema 渲染路径不依赖 EditorRenderLayer 的 private 静态方法，
// 这样 schema 模块在 panel 拆分后仍可独立使用。
//
// 返回 (open, requestRemove) 组合：第二参数 outRemove 帧内"用户在右键
// 菜单点了 Remove Component"标志。
bool ComponentHeaderLocal(const char* label, bool* outRemove,
                          bool removable, bool defaultOpen = true)
{
    if (outRemove != nullptr) { *outRemove = false; }
    int flags = ImGuiTreeNodeFlags_AllowOverlap;
    if (defaultOpen) { flags |= ImGuiTreeNodeFlags_DefaultOpen; }
    const bool open = ImGui::CollapsingHeader(label, flags);
    if (removable && outRemove != nullptr && ImGui::BeginPopupContextItem(label))
    {
        if (ImGui::MenuItem("Remove Component")) { *outRemove = true; }
        ImGui::EndPopup();
    }
    return open;
}

// 把 schema 内的某个字段写回 component；schema.get 每次都用最新 entity 解引，
// 应对 Undo/Redo 中 component 被 Remove → 重新 AddComponent 后地址变化。
//
// SetFn 是 PropertyDescriptor::SetFn（capture-less function ptr）；这里
// 包装成 std::function 让 SetFieldValueCommand 持有。
template <typename T>
auto MakeFieldApply(Orange::Engine::World*       pWorld,
                    Orange::Engine::Entity       entity,
                    const ComponentSchema*       pSchema,
                    PropertyDescriptor::SetFn    setFn)
{
    return [pWorld, entity, pSchema, setFn](const T& value)
    {
        if (pWorld == nullptr || pSchema == nullptr || pSchema->get == nullptr
            || setFn == nullptr)
        {
            return;
        }
        void* component = pSchema->get(*pWorld, entity);
        if (component != nullptr)
        {
            setFn(component, &value);
        }
    };
}

// 单个 property 的 ImGui 控件渲染 + 命令推送。
//
// fieldKey 是 caller 已拼好的 "{type}.{prop}" 字符串副本（避免 lambda 内
// 捕获 const char*，因为 SchemaInspector::DrawComponentSchemaSection 之后
// fieldKey 字符串本体会消失）。SetFieldValueCommand 复制 string 入栈，
// 这里只要保证 Push 调用时 fieldKey 是有效的 std::string 即可。
void DrawProperty(EditorHost&                  host,
                  Orange::Engine::Entity       entity,
                  const ComponentSchema&       schema,
                  const PropertyDescriptor&    prop,
                  void*                        component,
                  const std::string&           fieldKey)
{
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr || prop.get == nullptr || prop.set == nullptr) { return; }

    switch (prop.type)
    {
        case PropertyType::Float:
        {
            float oldVal = 0.0f;
            prop.get(component, &oldVal);
            float newVal = oldVal;
            const float minV = prop.attribs.hasRange ? prop.attribs.minValue : 0.0f;
            const float maxV = prop.attribs.hasRange ? prop.attribs.maxValue : 0.0f;
            if (ImGui::DragFloat(prop.label, &newVal, prop.attribs.dragSpeed, minV, maxV))
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<float>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<float>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Int:
        {
            int oldVal = 0;
            prop.get(component, &oldVal);
            int newVal = oldVal;
            const int minV = prop.attribs.hasRange ? static_cast<int>(prop.attribs.minValue) : 0;
            const int maxV = prop.attribs.hasRange ? static_cast<int>(prop.attribs.maxValue) : 0;
            if (ImGui::DragInt(prop.label, &newVal, prop.attribs.dragSpeed, minV, maxV))
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<int>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<int>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::UInt:
        {
            unsigned int oldVal = 0u;
            prop.get(component, &oldVal);
            // ImGui 无 DragUInt；用 DragScalar 给 U32 类型。
            unsigned int newVal = oldVal;
            const unsigned int minV = prop.attribs.hasRange
                ? static_cast<unsigned int>(prop.attribs.minValue) : 0u;
            const unsigned int maxV = prop.attribs.hasRange
                ? static_cast<unsigned int>(prop.attribs.maxValue) : 0u;
            if (ImGui::DragScalar(prop.label, ImGuiDataType_U32, &newVal,
                                  prop.attribs.dragSpeed, &minV, &maxV))
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<unsigned int>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<unsigned int>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Bool:
        {
            bool oldVal = false;
            prop.get(component, &oldVal);
            bool newVal = oldVal;
            if (ImGui::Checkbox(prop.label, &newVal))
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<bool>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<bool>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Vec2:
        {
            glm::vec2 oldVal{0.0f};
            prop.get(component, &oldVal);
            glm::vec2 newVal = oldVal;
            const float minV = prop.attribs.hasRange ? prop.attribs.minValue : 0.0f;
            const float maxV = prop.attribs.hasRange ? prop.attribs.maxValue : 0.0f;
            if (ImGui::DragFloat2(prop.label, &newVal.x,
                                  prop.attribs.dragSpeed, minV, maxV))
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec2>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<glm::vec2>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Vec3:
        {
            glm::vec3 oldVal{0.0f};
            prop.get(component, &oldVal);
            glm::vec3 newVal = oldVal;
            bool changed = false;
            if (prop.attribs.isColor)
            {
                // ColorEdit3 默认 0..1；HDR float 字段超出 1.0 时调用方该
                // 走 ColorEdit4 + HDR flag。当前内置 component（DirectionalLight
                // / Renderable tint）color 限定在 LDR 范围，这里走标准 ColorEdit3。
                changed = ImGui::ColorEdit3(prop.label, &newVal.x);
            }
            else
            {
                changed = DragVec3Colored(prop.label, &newVal.x,
                                          prop.attribs.dragSpeed);
            }
            if (changed)
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<glm::vec3>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Vec4:
        {
            glm::vec4 oldVal{0.0f};
            prop.get(component, &oldVal);
            glm::vec4 newVal = oldVal;
            bool changed = false;
            if (prop.attribs.isColor)
            {
                changed = ImGui::ColorEdit4(prop.label, &newVal.x);
            }
            else
            {
                const float minV = prop.attribs.hasRange ? prop.attribs.minValue : 0.0f;
                const float maxV = prop.attribs.hasRange ? prop.attribs.maxValue : 0.0f;
                changed = ImGui::DragFloat4(prop.label, &newVal.x,
                                            prop.attribs.dragSpeed, minV, maxV);
            }
            if (changed)
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec4>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<glm::vec4>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Quat:
        {
            // 当前没有内置 component 通过 schema 暴露 quat（Transform.rotation
            // 走 Euler 缓存的混合路径，留到后续 commit 处理）。这里给个
            // 显式的"未在 schema 路径渲染"placeholder，方便日后启用。
            ImGui::TextDisabled("%s (quat — not yet schema-rendered)", prop.label);
            break;
        }
        case PropertyType::String:
        {
            std::string oldVal;
            prop.get(component, &oldVal);
            // 用静态 buffer 给 ImGui InputText 写——避免每帧 push std::string
            // back-and-forth。256 字节符合 Inspector typical entity name 长度。
            // 切实体时 ImGui ID 不同，buffer 内容也会被刷新（用户切走再切回
            // 看到的就是最新值）。
            char buffer[256] = {};
            const std::size_t copyN = std::min<std::size_t>(oldVal.size(),
                                                            sizeof(buffer) - 1);
            std::memcpy(buffer, oldVal.data(), copyN);
            if (ImGui::InputText(prop.label, buffer, sizeof(buffer)))
            {
                std::string newVal{buffer};
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<std::string>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<std::string>(pWorld, entity, &schema, prop.set)));
            }
            break;
        }
    }

    // 通用 tooltip：所有 PropertyType 共用。注意 isItemHovered 必须**紧跟
    // 上面控件**调用——switch 内每个 case 已 Pop 控件，hover 检查在 switch
    // 之外是对"最后一个 ImGui item（也就是上面渲染的控件）"。
    if (prop.attribs.tooltip != nullptr && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", prop.attribs.tooltip);
    }
}

}  // anonymous namespace

void DrawComponentSchemaSection(EditorHost&                  host,
                                Orange::Engine::Entity       entity,
                                const ComponentSchema&       schema)
{
    if (schema.has == nullptr || schema.get == nullptr) { return; }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return; }
    if (!schema.has(*pWorld, entity)) { return; }

    void* component = schema.get(*pWorld, entity);
    if (component == nullptr) { return; }

    bool requestRemove = false;
    const bool open = ComponentHeaderLocal(
        schema.displayName ? schema.displayName : schema.typeName,
        &requestRemove, /*removable=*/schema.remove != nullptr);

    if (open)
    {
        for (const auto& prop : schema.properties)
        {
            // 字段 key："{type}.{prop}" —— 与既有 SetFieldValueCommand 风格
            // 一致；确保 coalesce 在同一字段连续编辑时合并成一条命令。
            std::string fieldKey;
            fieldKey.reserve(64);
            fieldKey.append(schema.typeName ? schema.typeName : "?");
            fieldKey.push_back('.');
            fieldKey.append(prop.name ? prop.name : "?");
            DrawProperty(host, entity, schema, prop, component, fieldKey);
        }
    }

    if (requestRemove && schema.remove != nullptr)
    {
        schema.remove(*pWorld, entity);
        // 破坏性操作清掉 undo 历史——同 v0.2 期 DrawInspectorXxx 移除路径
        // 的一贯做法（命令栈内的字段编辑 lambda 仍指向已 destroy 的
        // component 槽位，下一次 Undo 会触发 entt assert）。
        host.cmdStack.Clear();
    }
}

void DrawEntityViaSchemas(EditorHost& host, Orange::Engine::Entity entity)
{
    auto& reg = ComponentSchemaRegistry::Instance();
    for (const auto& schema : reg.All())
    {
        DrawComponentSchemaSection(host, entity, schema);
    }
}

}  // namespace Orange::Editor::Schema
