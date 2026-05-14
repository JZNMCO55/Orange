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
#include "../plugin/IEditorInspectorPlugin.h"
#include "ComponentSchemaRegistry.h"

#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <cstdint>
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
// c14 起 lambda 捕获 `EditorHost*` 而非 `World*`：调用时 `pHost->scene
// .pWorld.get()` 间接解 World——切场景后 host.scene.pWorld 换新指针 / 置
// 空，命令 lambda 走 nullptr 防御分支 no-op，不再因 dangling World* 崩溃。
// 详细纪律见 command/EntityCommands.h 顶注释。
//
// SetFn 是 PropertyDescriptor::SetFn（capture-less function ptr）；这里
// 包装成 std::function 让 SetFieldValueCommand 持有。
template <typename T>
auto MakeFieldApply(EditorHost*                  pHost,
                    Orange::Engine::Entity       entity,
                    const ComponentSchema*       pSchema,
                    PropertyDescriptor::SetFn    setFn)
{
    return [pHost, entity, pSchema, setFn](const T& value)
    {
        if (pHost == nullptr || pSchema == nullptr || pSchema->get == nullptr
            || setFn == nullptr)
        {
            return;
        }
        auto* pWorld = pHost->scene.pWorld.get();
        if (pWorld == nullptr) { return; }   // 漏 Clear 的安全降级
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
    if (pWorld == nullptr) { return; }

    // 条件可见：visibleIf 返回 false 时整段跳过（含 GroupSeparator / 控件 /
    // tooltip）。典型用例 ColliderComponent.shape 的 variant 分支——非当前
    // alternative 的字段全部隐藏。空 visibleIf = 总显示。
    if (prop.attribs.visibleIf != nullptr
        && !prop.attribs.visibleIf(component))
    {
        return;
    }

    // 视觉分组分隔符：注册时挂在 group 第一个字段上，在该字段控件**之前**
    // 渲染 SeparatorText。等价 v0.1 期 EditorRenderLayer 内手写
    // `ImGui::SeparatorText("Lifetime")` 等分组提示。
    //
    // 注意：本检查在 get/set null 早退**之前**——这样允许 Builder::Group(...)
    // 注册"纯 header 段"（typeName 占位 + get/set 均为 nullptr），仅显示
    // SeparatorText 文本而无可编辑控件（Polygon / EdgeChain 的零字段段用例）。
    if (prop.attribs.groupSeparator != nullptr)
    {
        ImGui::SeparatorText(prop.attribs.groupSeparator);
    }

    // 无 get → 该字段是 Group-only 占位，仅显示 SeparatorText 后返回。
    // 无 set 但 get 有效：允许 readOnly 字段以 nullptr setter 注册（典型
    // 用例：AnimatorComponent.backend 名只读显示，无可编辑路径）；非
    // readOnly 字段仍要求 set 有效（否则字段在 UI 上能拖但写不回，更糟）。
    if (prop.get == nullptr) { return; }
    if (prop.set == nullptr && !prop.attribs.readOnly) { return; }

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
                    MakeFieldApply<float>(&host, entity, &schema, prop.set)));
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
                    MakeFieldApply<int>(&host, entity, &schema, prop.set)));
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
                    MakeFieldApply<unsigned int>(&host, entity, &schema, prop.set)));
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
                    MakeFieldApply<bool>(&host, entity, &schema, prop.set)));
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
                    MakeFieldApply<glm::vec2>(&host, entity, &schema, prop.set)));
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
                    MakeFieldApply<glm::vec3>(&host, entity, &schema, prop.set)));
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
                    MakeFieldApply<glm::vec4>(&host, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::Quat:
        {
            // 旋转字段走 "DragFloat3 Euler 缓存 + apply 时回算 quat" 混合路径：
            //   * 直接 DragFloat4 quat 不直观（用户不可能心算 (w,x,y,z) 单位
            //     四元数）；用 Euler 给视觉
            //   * 但 quat→Euler 在 gimbal lock 附近不连续，每帧从 quat 重推
            //     Euler 会让 DragFloat3 滑动时数字跳——所以**编辑期**缓存
            //     Euler 在 EditorSelection 内，仅在切实体时从 quat 重算一次
            //   * Undo / Redo 路径在 apply lambda 里把缓存 entity 置 Invalid，
            //     下一帧 Quat case 看到 cacheEntity != entity 自动从最新 quat
            //     重算 Euler 显示
            //
            // 等价于 v0.1 期 EditorRenderLayer::DrawInspectorTransform 内
            // rotation 段（已删）。所有 Transform.rotation 编辑路径都汇入此 case。
            glm::quat oldVal{1.0f, 0.0f, 0.0f, 0.0f};
            prop.get(component, &oldVal);

            if (host.selection.transformEulerCacheEntity != entity)
            {
                const glm::vec3 eulerRad = glm::eulerAngles(oldVal);
                host.selection.transformEulerCache       = glm::degrees(eulerRad);
                host.selection.transformEulerCacheEntity = entity;
            }

            const float dragSpeed = (prop.attribs.dragSpeed > 0.0f)
                                  ? prop.attribs.dragSpeed : 0.5f;
            if (DragVec3Colored(prop.label,
                                &host.selection.transformEulerCache.x,
                                dragSpeed))
            {
                const glm::quat newVal =
                    glm::quat(glm::radians(host.selection.transformEulerCache));
                prop.set(component, &newVal);

                // apply lambda 不复用通用 MakeFieldApply<glm::quat>——多一步
                // invalidate Euler 缓存。c14 起仅 capture pHost（不再 capture
                // pWorld），lambda 内 `pHost->scene.pWorld.get()` 间接解 World——
                // 切场景时 host.scene.pWorld 换新指针 / 置空，命令走 nullptr
                // 防御分支 no-op，不再因 dangling 崩。
                auto*                       pHost      = &host;
                PropertyDescriptor::SetFn   setFn      = prop.set;
                const ComponentSchema*      pSchema    = &schema;
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::quat>>(
                    entity, fieldKey, oldVal, newVal,
                    [pHost, entity, pSchema, setFn]
                    (const glm::quat& v)
                    {
                        if (pHost == nullptr || pSchema == nullptr
                            || pSchema->get == nullptr || setFn == nullptr)
                        {
                            return;
                        }
                        auto* pW = pHost->scene.pWorld.get();
                        if (pW == nullptr) { return; }
                        void* c = pSchema->get(*pW, entity);
                        if (c != nullptr) { setFn(c, &v); }
                        pHost->selection.transformEulerCacheEntity =
                            Orange::Engine::Entity::Invalid();
                    }));
            }
            break;
        }
        case PropertyType::Enum:
        {
            // Enum 走 int marshal —— Builder::FieldEnum 内 get/set 已把
            // 枚举值与 int 互转。控件用 ImGui::Combo：enumNames 缺失或
            // count <= 0 时降级显示 "(no enum names)" placeholder（开发
            // 期 schema 注册漏写 .EnumNames(...) 的兜底）。
            int oldVal = 0;
            prop.get(component, &oldVal);
            int newVal = oldVal;
            if (prop.attribs.enumNames == nullptr || prop.attribs.enumCount <= 0)
            {
                ImGui::TextDisabled("%s (enum: no names — schema bug)", prop.label);
                break;
            }
            // ImGui::Combo 对越界 current item 显示空；这里把超界值钳进
            // [0, enumCount) 仅用于显示，underlying enum 数据本身不改
            // （直到用户实际选了新项才走 set 路径）。
            int displayIdx = newVal;
            if (displayIdx < 0 || displayIdx >= prop.attribs.enumCount)
            {
                displayIdx = 0;
            }
            if (ImGui::Combo(prop.label, &displayIdx,
                             prop.attribs.enumNames, prop.attribs.enumCount))
            {
                newVal = displayIdx;
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<int>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<int>(&host, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::EntityRef:
        {
            // Entity 引用字段——本 commit 只读显示。"#<id>" / "(none)"。
            // 当前用例：Hierarchy.parent / firstChild / prevSibling / nextSibling，
            // 这四个字段由 Entity Tree 的 DnD reparent 路径管理；Inspector 内
            // 直接编辑反而会与 DnD 状态机竞争。
            //
            // 后续可扩展为 drag-drop 写入：在 ImGui::Text 后追加
            // BeginDragDropTarget / AcceptDragDropPayload("ORANGE_ENTITY") 路径
            // + Push SetFieldValueCommand<Orange::Engine::Entity>。本 commit 不做。
            Orange::Engine::Entity oldVal = Orange::Engine::Entity::Invalid();
            prop.get(component, &oldVal);
            if (oldVal.IsValid())
            {
                ImGui::Text("%s: #%u", prop.label,
                            static_cast<unsigned>(
                                static_cast<std::uint32_t>(oldVal.Value())));
            }
            else
            {
                ImGui::Text("%s: (none)", prop.label);
            }
            break;
        }
        case PropertyType::String:
        {
            std::string oldVal;
            prop.get(component, &oldVal);

            // readOnly 路径：仅显示 "<label>: <value>"——左侧 ImGui::Text 写
            // 静态 label，SameLine 后 TextDisabled 写动态 value。不画 InputText、
            // 不 Push 命令。当前 readOnly 仅 String case 支持（c9 最小集）；其
            // 他 PropertyType 上设 readOnly 暂被忽略走默认编辑控件。
            if (prop.attribs.readOnly)
            {
                ImGui::Text("%s:", prop.label);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", oldVal.c_str());
                break;
            }

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
                    MakeFieldApply<std::string>(&host, entity, &schema, prop.set)));
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
        // Plugin 调度：遍历 host.inspectorPlugins，第一条 CanHandle == true 接
        // 管本 component 段。语义按 IEditorInspectorPlugin.h "调用约定" 节：
        //   * ParseBegin 返 true → 跳过默认 properties 渲染（plugin 自渲染整段）
        //   * ParseBegin 返 false → 继续默认渲染
        //   * 默认渲染（或自渲染）之后**总是**调 ParseEnd 追加扩展 UI
        // 多 plugin 都 CanHandle 时按注册顺序取第一条；后续 plugin 不再调度。
        Orange::Editor::Plugin::IEditorInspectorPlugin* pActivePlugin = nullptr;
        bool pluginTakesOver = false;
        for (auto& pPlugin : host.inspectorPlugins)
        {
            if (pPlugin != nullptr && pPlugin->CanHandle(schema))
            {
                pActivePlugin   = pPlugin.get();
                pluginTakesOver = pActivePlugin->ParseBegin(host, entity, schema, component);
                break;
            }
        }

        if (!pluginTakesOver)
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

        if (pActivePlugin != nullptr)
        {
            pActivePlugin->ParseEnd(host, entity, schema, component);
        }
    }

    if (requestRemove && schema.remove != nullptr)
    {
        schema.remove(*pWorld, entity);
        // Transform Euler 缓存与 selectedEntity 联动；任意 component 被
        // Remove 都顺手 invalidate 一下：避免 Remove Transform 后再
        // AddComponent 时 cacheEntity 仍等于当前 entity → Quat case 直接
        // 走旧 cache 值（导致显示错位）。invalidate 无害——下一帧 Quat
        // case 看到 cacheEntity != entity 会从最新 quat 重算 Euler；如果
        // 该 entity 已经没有 TransformComponent，Quat case 根本不会进入。
        host.selection.transformEulerCacheEntity =
            Orange::Engine::Entity::Invalid();
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
