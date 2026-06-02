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

#include "../BuiltinAssets.h"   // EnsureMaterialInstance（Material AssetRef lazy 注册）
#include "../EditorHost.h"
#include "../EditorWidgets.h"
#include "../command/LambdaCommand.h"
#include "../command/SetFieldValueCommand.h"
#include "../plugin/IEditorInspectorPlugin.h"
#include "../theme/EditorTheme.h"
#include "ComponentSchemaRegistry.h"

#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
//
// v0.6.5 c5：bandColor 参数承载 per-component-type 4px 色带色（方向 C
// "识别度补丁"）。color.w == 0 时不画色带（向后兼容 / fallback）。色带
// 在 CollapsingHeader 渲染完成后通过 DrawList 叠加在 header 左侧，**遮
// 盖** header 本身的左 4px 区域；这意味着 header 内左侧很窄的一段会被
// 色带覆盖（折叠箭头 ▶ 通常在色带右侧 framePadding 处，不受影响）。
bool ComponentHeaderLocal(const char* label, bool* outRemove,
                          bool removable, const ImVec4& bandColor,
                          bool defaultOpen = true)
{
    if (outRemove != nullptr) { *outRemove = false; }
    int flags = ImGuiTreeNodeFlags_AllowOverlap;
    if (defaultOpen) { flags |= ImGuiTreeNodeFlags_DefaultOpen; }
    const bool open = ImGui::CollapsingHeader(label, flags);

    // 4px 色带：取 CollapsingHeader 渲染后的 item rect，左边缘画 4px
    // 实色矩形。bandColor.w > 0 才画（== 0 视为 "不画"）。
    if (bandColor.w > 0.0f)
    {
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const float  bandW   =
            Orange::Editor::Theme::ComponentTypeBand::GetBandWidthPx();
        const ImU32  bandU32 = ImGui::ColorConvertFloat4ToU32(bandColor);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(itemMin.x, itemMin.y),
            ImVec2(itemMin.x + bandW, itemMax.y),
            bandU32);
    }

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

// MakeFieldApply 的 AssetRef 变体：setter 多带 `const EditorAssetContext&`
// 参数。命令 replay 时通过 capture 的 EditorHost* 拿到 `host.assets`，
// 转交给新签名 setter。EditorAssetContext 与 EditorHost 同生命周期（host
// 持有 EditorAssetContext 子结构），不会出现 dangling 引用；World 已置空
// 走 nullptr 早退分支。
template <typename T>
auto MakeAssetRefFieldApply(EditorHost*                          pHost,
                            Orange::Engine::Entity               entity,
                            const ComponentSchema*               pSchema,
                            PropertyDescriptor::AssetRefSetFn    setFn)
{
    return [pHost, entity, pSchema, setFn](const T& value)
    {
        if (pHost == nullptr || pSchema == nullptr || pSchema->get == nullptr
            || setFn == nullptr)
        {
            return;
        }
        auto* pWorld = pHost->scene.pWorld.get();
        if (pWorld == nullptr) { return; }
        void* component = pSchema->get(*pWorld, entity);
        if (component != nullptr)
        {
            setFn(component, pHost->assets, &value);
        }
    };
}

// multi-edit 写回广播（§2.4/P1）：把 primary 刚改的字段值写到选区其余选中
// 实体的**同 component 同字段**，每个 follower 一条 SetFieldValueCommand
// （其自身旧值→新值）。caller 在 selCount>1 时围绕本次拖动开 cmdStack group，
// 把 primary + 所有 follower 命令收束成一次 Undo。
//
// 零回归保证：additionalSelectedEntities 为空（单选）→ 立即返回，不触碰任何
// 状态。follower 缺该 component（异构多选）→ schema.get 返回 nullptr 跳过。
// 仅普通 get/set 字段走本路径（Float/Int/UInt/Bool/Vec2/3/4/Quat/Enum）；
// AssetRef（assetRefGet/Set + EditorAssetContext）/ EntityRef（只读）不广播。
template <typename T>
void BroadcastFieldToSelection(EditorHost&                  host,
                               const ComponentSchema&       schema,
                               const PropertyDescriptor&    prop,
                               const std::string&           fieldKey,
                               const T&                     newVal)
{
    if (host.selection.additionalSelectedEntities.empty()) { return; }
    if (schema.get == nullptr || prop.get == nullptr || prop.set == nullptr) { return; }
    auto* pWorld = host.scene.pWorld.get();
    if (pWorld == nullptr) { return; }
    for (const auto other : host.selection.additionalSelectedEntities)
    {
        if (!pWorld->IsValid(other)) { continue; }
        void* otherComp = schema.get(*pWorld, other);
        if (otherComp == nullptr) { continue; }  // 该实体无此 component → 跳过
        T otherOld{};
        prop.get(otherComp, &otherOld);
        prop.set(otherComp, &newVal);
        host.cmdStack.Push(std::make_unique<SetFieldValueCommand<T>>(
            other, fieldKey, otherOld, newVal,
            MakeFieldApply<T>(&host, other, &schema, prop.set)));
    }
}

// multi-edit 拖动分组：在写入本字段的**第一条**命令之前调，确保 primary +
// 所有 follower 命令落入同一 cmdStack group → 一次 Undo 撤全部。单选
// （additionalSelectedEntities 空）时 no-op = 零回归。
//
// 为何不沿用 switch 之后的 IsItemActivated 开组（旧实现 bug，dogfood 暴露
// "多选拖共有字段后 Ctrl+Z 一次回不到初值"）：DragFloat2/3/4（Vec 字段如
// position / scale）内部是 ImGui BeginGroup + N 个子 DragScalar，EndGroup 后
// last-item 是无 ID 的布局 group——IsItemActivated() / IsItemActive() 对它匹配
// 不到 ActiveId 而失效，拖 x/y/z 轴时 group 根本不开，primary + follower 命令
// 每帧散落上栈（相邻 entity 不同 → coalesce 也失效），Ctrl+Z 一次只撤一条。
// 改为"本帧值已变 + 尚未开组"即开组（在产生命令前调，不依赖 IsItem* 激活检测），
// 对单分量 / 多分量 / Checkbox / Combo 一致可靠。关组仍由 switch 之后的
// IsItemDeactivated 负责——EndGroup 把子项 Deactivated 聚合到布局 group，对
// 多分量可靠（ImGui "拖动结束提交" 官方用法）。
//
// group name 用静态字面量而非 fieldKey.c_str()——CommandStack 不复制 name 且
// group 跨帧存活（开组在拖动起帧、关组在释放帧），传 DrawProperty 局部
// std::string 的 c_str() 会悬空（见 CommandStack.h / CommandStack.cpp name
// 生命周期契约）。Disable 模式下 name 仅作 label、不参与 merge，固定字面量即可。
// 标记 multi-edit 自己开启、尚未关闭的 group —— 区别于 gizmo 等其它 group 来
// 源，让关组只收束自己开的组（也不被别人的 group 误关）。file-scope 单飞：同
// 一时刻只有一个字段控件在拖动，不会并发开多组。
bool sMultiEditGroupOpen = false;

inline void EnsureMultiEditGroup(EditorHost& host)
{
    if (!host.selection.additionalSelectedEntities.empty()
        && !host.cmdStack.InGroup())
    {
        host.cmdStack.BeginGroup("Edit Field (multi-select)", MergeMode::Disable);
        sMultiEditGroupOpen = true;
    }
}

// 关组：仅当本文件开的 multi-edit group 仍开着（sMultiEditGroupOpen）且拖动 /
// 编辑已结束（全局无任何 ImGui item active）时收束。
//
// 为何不用 IsItemDeactivated（旧实现，dogfood 实测崩溃暴露）：DragFloat2/3/4 的
// 无 ID 布局 group 让 IsItemDeactivated() 在释放帧返回 false → EndGroup 永不触发
// → 组泄漏：当帧 pending 未打包入栈"无法 Undo"，且 mInGroup 滞留 true，下次
// gizmo 等再调 BeginGroup 撞 `assert(!mInGroup)` 崩溃。改用 !IsAnyItemActive：
// DragFloatN 释放后 ActiveId 归 0，对多分量可靠。sMultiEditGroupOpen 守住"只关
// 自己的组"——gizmo 拖动期间（其自有 group）本检查 sMultiEditGroupOpen==false
// 不会误关。
inline void CloseMultiEditGroupIfDone(EditorHost& host)
{
    if (sMultiEditGroupOpen && !ImGui::IsAnyItemActive())
    {
        if (host.cmdStack.InGroup()) { host.cmdStack.EndGroup(); }
        sMultiEditGroupOpen = false;
    }
}

// RemoveComponent-undo 基建：移除组件前，按 schema 字段把当前值快照成一组
// "restorer" 闭包（每个闭包持有该字段的值副本 + setter，给定新组件指针即把
// 该字段写回）。undo 时先 schema.add 重建默认组件、再跑所有 restorer 还原
// 原始字段值——实现"删组件可 Undo 且数据不丢"（gap 报告 §2.4/P1 破坏性
// undo，限实体存活的组件级，绕开删实体的 EnTT id 稳定性难题）。
//
// 覆盖普通 get/set 字段（Float/Int/UInt/Bool/Vec2/3/4/Quat/Enum/EntityRef）
// + AssetRef（assetRefGet/Set，restorer 持 host 拿 EditorAssetContext）。
// group-only / readOnly（无 set）字段跳过（无从还原）。
std::vector<std::function<void(void*)>>
CaptureComponentState(EditorHost& host, const ComponentSchema& schema, const void* component)
{
    std::vector<std::function<void(void*)>> restorers;
    for (const auto& prop : schema.properties)
    {
        // AssetRef / AssetRefArray：用 assetRefGet 取路径，restorer 经
        // host.assets 写回。两者 marshal 形态不同（string vs vector<string>），
        // 必须按 prop.type 分流——否则把 vector<string> 内存当 string 读会损坏。
        if (prop.assetRefGet != nullptr && prop.assetRefSet != nullptr)
        {
            const PropertyDescriptor::AssetRefSetFn setFn = prop.assetRefSet;
            auto* pH = &host;
            if (prop.type == PropertyType::AssetRefArray)
            {
                std::vector<std::string> v;
                prop.assetRefGet(component, host.assets, &v);
                restorers.push_back([v, setFn, pH](void* c) { setFn(c, pH->assets, &v); });
            }
            else
            {
                std::string v;
                prop.assetRefGet(component, host.assets, &v);
                restorers.push_back([v, setFn, pH](void* c) { setFn(c, pH->assets, &v); });
            }
            continue;
        }
        if (prop.get == nullptr || prop.set == nullptr) { continue; }
        const PropertyDescriptor::SetFn setFn = prop.set;
        switch (prop.type)
        {
            case PropertyType::Float:
            { float v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::Int:
            case PropertyType::Enum:
            { int v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::UInt:
            { unsigned int v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::Bool:
            { bool v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::Vec2:
            { glm::vec2 v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::Vec3:
            { glm::vec3 v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::Vec4:
            { glm::vec4 v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::Quat:
            { glm::quat v{1.0f, 0.0f, 0.0f, 0.0f}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            case PropertyType::EntityRef:
            { Orange::Engine::Entity v{}; prop.get(component, &v); restorers.push_back([v, setFn](void* c){ setFn(c, &v); }); break; }
            default: break;
        }
    }
    return restorers;
}

// 单个 property 的 ImGui 控件渲染 + 命令推送。**调用前提**：caller 已在
// PropertyTable 内（Widgets::BeginPropertyTable 已 return true），DrawProperty
// 内通过 Widgets::PropertyLabel 写左列 label + 触发右列 SetNextItemWidth
// (-FLT_MIN)，然后画占满右列的控件。所有控件 ImGui 调用都用 "##<name>"
// 形式的隐藏 label —— 真实 label 已经被 PropertyLabel 在左列单独显示。
//
// 分段（GroupSeparator）+ 可见性（visibleIf）+ get/set null 早退**不**在
// 本函数处理，由 DrawComponentSchemaSection 在调用 DrawProperty 之前过
// 滤；进到本函数的 prop 一定要渲染。
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

    // 左列 label + 右列 SetNextItemWidth(-FLT_MIN)；tooltip 挂在 label 上
    // 而非控件上（控件拖拽 / 编辑状态时 hover 会被打断；label hover 更稳定）。
    Orange::Editor::Widgets::PropertyLabel(prop.label, prop.attribs.tooltip);

    // PushID(prop.name) 隔离同一 component schema 段内多个控件的 ImGui ID。
    // v0.4.5 c1 修复：之前所有控件 label 用同一个 "##v"，ImGui ID =
    // hash("##v") + ID stack 在同 Table 内**共享**——hover 一个控件会让
    // 所有同 ID 控件被算作 hover，红色高亮 + tooltip 在错误位置弹出。
    // PropertyLabel 内 TextUnformatted 不产生 ID，所以 PushID 放在 Label
    // 之后、控件之前都可——这里放控件之前最直观。
    ImGui::PushID(prop.name != nullptr ? prop.name : "?");

    // 紧跟 PropertyLabel 的下一个 ImGui 控件占满右列。控件 label 一律用
    // "##" 前缀隐藏 —— 真实 label 已经被 PropertyLabel 写在左列。
    switch (prop.type)
    {
        case PropertyType::Float:
        {
            float oldVal = 0.0f;
            prop.get(component, &oldVal);
            float newVal = oldVal;
            const float minV = prop.attribs.hasRange ? prop.attribs.minValue : 0.0f;
            const float maxV = prop.attribs.hasRange ? prop.attribs.maxValue : 0.0f;
            if (ImGui::DragFloat("##v", &newVal, prop.attribs.dragSpeed, minV, maxV))
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<float>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<float>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<float>(host, schema, prop, fieldKey, newVal);
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
            if (ImGui::DragInt("##v", &newVal, prop.attribs.dragSpeed, minV, maxV))
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<int>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<int>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<int>(host, schema, prop, fieldKey, newVal);
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
            if (ImGui::DragScalar("##v", ImGuiDataType_U32, &newVal,
                                  prop.attribs.dragSpeed, &minV, &maxV))
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<unsigned int>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<unsigned int>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<unsigned int>(host, schema, prop, fieldKey, newVal);
            }
            break;
        }
        case PropertyType::Bool:
        {
            bool oldVal = false;
            prop.get(component, &oldVal);
            bool newVal = oldVal;
            if (ImGui::Checkbox("##v", &newVal))
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<bool>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<bool>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<bool>(host, schema, prop, fieldKey, newVal);
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
            if (ImGui::DragFloat2("##v", &newVal.x,
                                  prop.attribs.dragSpeed, minV, maxV))
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec2>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<glm::vec2>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<glm::vec2>(host, schema, prop, fieldKey, newVal);
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
                changed = ImGui::ColorEdit3("##v", &newVal.x);
            }
            else
            {
                changed = DragVec3Colored(prop.label, &newVal.x,
                                          prop.attribs.dragSpeed);
            }
            if (changed)
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec3>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<glm::vec3>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<glm::vec3>(host, schema, prop, fieldKey, newVal);
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
                changed = ImGui::ColorEdit4("##v", &newVal.x);
            }
            else
            {
                const float minV = prop.attribs.hasRange ? prop.attribs.minValue : 0.0f;
                const float maxV = prop.attribs.hasRange ? prop.attribs.maxValue : 0.0f;
                changed = ImGui::DragFloat4("##v", &newVal.x,
                                            prop.attribs.dragSpeed, minV, maxV);
            }
            if (changed)
            {
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<glm::vec4>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<glm::vec4>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<glm::vec4>(host, schema, prop, fieldKey, newVal);
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

                // apply lambda 只设 quat 值，**不**在此 invalidate Euler 缓存。
                // 原先在此 invalidate 本意是 Undo/Redo 后刷新 Euler 显示，但
                // CommandStack::Push 会立即 Execute 命令（且 DragFloat3 连续拖动
                // 走 coalesce 分支每 tick 重新 Execute），导致 live 拖动期间缓存
                // 每帧被失效 → 下一帧 Quat case 从 quat 重算 Euler，撞
                // glm::eulerAngles 的 pitch asin 值域 [-90°,90°] 折返，表现为
                // "旋转过不了 90°"。改为：Euler 缓存只在切换 entity（本 case 顶部）
                // 与 Undo/Redo 后（EditorRenderLayer::ValidateEntityHandles 无条件
                // 失效）刷新；live 拖动期间持续保留，允许任意角度连续累加。
                // c14 起仅 capture pHost（不再 capture pWorld），lambda 内
                // `pHost->scene.pWorld.get()` 间接解 World——切场景时换新指针 /
                // 置空，走 nullptr 防御分支 no-op，不因 dangling 崩。
                auto*                       pHost      = &host;
                PropertyDescriptor::SetFn   setFn      = prop.set;
                const ComponentSchema*      pSchema    = &schema;
                EnsureMultiEditGroup(host);
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
                    }));
                // follower 不显示，无需 Euler 缓存，走通用 quat set 广播即可。
                BroadcastFieldToSelection<glm::quat>(host, schema, prop, fieldKey, newVal);
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
                // 右列显示开发期 hint；左列的 prop.label 已经被 PropertyLabel 写过。
                ImGui::TextDisabled("(enum: no names — schema bug)");
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
            if (ImGui::Combo("##v", &displayIdx,
                             prop.attribs.enumNames, prop.attribs.enumCount))
            {
                newVal = displayIdx;
                prop.set(component, &newVal);
                EnsureMultiEditGroup(host);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<int>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<int>(&host, entity, &schema, prop.set)));
                BroadcastFieldToSelection<int>(host, schema, prop, fieldKey, newVal);
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
            // 右列显示 entity 引用值；左列的 prop.label 由 PropertyLabel 写过。
            Orange::Engine::Entity oldVal = Orange::Engine::Entity::Invalid();
            prop.get(component, &oldVal);
            if (oldVal.IsValid())
            {
                ImGui::Text("#%u",
                            static_cast<unsigned>(
                                static_cast<std::uint32_t>(oldVal.Value())));
            }
            else
            {
                ImGui::TextDisabled("(none)");
            }
            break;
        }
        case PropertyType::AssetRef:
        {
            // v0.5 c4：完整 AssetRef 控件。左→右排列：
            //   [短名 / "(none)"] [×清除] [Pick 浏览器当前选中]
            // 整段 BeginDragDropTarget 接受 "ORANGE_ASSET" payload 写入字段。
            // 全路径走 hover tooltip（Inspector 右列宽有限），与 c1 显示
            // 风格一致。所有写入路径都 Push SetFieldValueCommand<std::string>
            // 让 Ctrl+Z / Ctrl+Y 与其他字段同款 undo / redo。
            //
            // v0.9.5 c2：双路径 dispatch —— `assetRefGet/Set` 非空时走带
            // `const EditorAssetContext&` 的新 accessor（c3 完成迁移后这是
            // 主路径）；否则走旧 `get/set`（兼容期防御，c3 完工后无内置
            // schema 再走该分支）。Undo/Redo replay 的 apply lambda 相应
            // 切换 MakeAssetRefFieldApply / MakeFieldApply。
            const bool useCtxAccessor = (prop.assetRefGet != nullptr)
                                     || (prop.assetRefSet != nullptr);
            const bool canRead  = useCtxAccessor ? (prop.assetRefGet != nullptr)
                                                  : (prop.get != nullptr);
            const bool canWrite = useCtxAccessor ? (prop.assetRefSet != nullptr)
                                                  : (prop.set != nullptr);
            auto readPath = [&](std::string& out)
            {
                if (useCtxAccessor)
                {
                    if (prop.assetRefGet != nullptr)
                    {
                        prop.assetRefGet(component, host.assets, &out);
                    }
                }
                else if (prop.get != nullptr)
                {
                    prop.get(component, &out);
                }
            };
            auto writePath = [&](const std::string& v)
            {
                // Material AssetRef:写入前先 lazy 注册到 namedMaterialInstances。
                // materialSet 只查表不 lazy load → 刚导入(glTF)/创建(Create
                // Material)的 .material 不在表里就赋不上(GAP-2026-05-25 用户
                // 反馈:导入/Water 材质拖不到 entity)。EnsureMaterialInstance 读
                // 文件 + CreateInstance + ApplyDataToInstance + 写表;之后
                // materialSet/undo-redo replay 都能查到。非 Material 字段
                // (Mesh 等)不走此路。
                if (prop.attribs.assetKind == AssetKind::Material && !v.empty())
                {
                    (void)::EnsureMaterialInstance(host, v);
                }
                if (useCtxAccessor)
                {
                    if (prop.assetRefSet != nullptr)
                    {
                        prop.assetRefSet(component, host.assets, &v);
                    }
                }
                else if (prop.set != nullptr)
                {
                    prop.set(component, &v);
                }
            };
            auto makeApply = [&]() -> SetFieldValueCommand<std::string>::ApplyFn
            {
                if (useCtxAccessor)
                {
                    return MakeAssetRefFieldApply<std::string>(
                        &host, entity, &schema, prop.assetRefSet);
                }
                return MakeFieldApply<std::string>(
                    &host, entity, &schema, prop.set);
            };

            std::string curPath;
            if (canRead) { readPath(curPath); }
            const std::string oldPath = curPath;

            // 显示当前 path 短名 / "(none)"。Selectable 让本段成为 DnD
            // target hit-rect（BeginDragDropTarget 要求"上一个 item 有 ID"，
            // TextUnformatted 无 ID 而 Selectable 有）。
            std::string_view displayLabel;
            std::string shortNameBuf;
            if (curPath.empty())
            {
                displayLabel = std::string_view{"(none)"};
            }
            else
            {
                const auto slash = curPath.find_last_of('/');
                shortNameBuf = (slash == std::string::npos)
                    ? curPath
                    : curPath.substr(slash + 1);
                displayLabel = shortNameBuf;
            }
            // 短名 cell width 留给 button：CalcItemWidth - 两个 SmallButton 宽。
            const ImGuiStyle& s = ImGui::GetStyle();
            const float btnXW   = ImGui::CalcTextSize("\xC3\x97").x
                                 + s.FramePadding.x * 2.0f;  // "×"
            const float btnPickW = ImGui::CalcTextSize("Pick").x
                                 + s.FramePadding.x * 2.0f;
            const float total   = ImGui::CalcItemWidth();
            const float nameW   = (std::max)(64.0f,
                total - btnXW - btnPickW - s.ItemSpacing.x * 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                  s.Colors[ImGuiCol_FrameBgHovered]);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                  s.Colors[ImGuiCol_FrameBgActive]);
            ImGui::Selectable(std::string(displayLabel).c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick,
                              ImVec2(nameW, 0));
            ImGui::PopStyleColor(3);
            if (!curPath.empty() && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", curPath.c_str());
            }
            // DnD target：接收 Asset 浏览器拖来的 path 字符串。payload
            // 含 '\0'，直接 std::string{data} 构造即可。
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ORANGE_ASSET"))
                {
                    const char* pData = static_cast<const char*>(payload->Data);
                    std::string newPath{pData};
                    if (newPath != oldPath && canWrite)
                    {
                        writePath(newPath);
                        host.cmdStack.Push(
                            std::make_unique<SetFieldValueCommand<std::string>>(
                                entity, fieldKey, oldPath, newPath, makeApply()));
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // 清除按钮（Codicons CLOSE）—— v0.6.5 c4 起从 UTF-8 "×"
            // U+00D7 切到 ICON_CI_CLOSE，与 LayersPanel "X" remove 按钮
            // 同款 icon 视觉。
            ImGui::SameLine();
            ImGui::BeginDisabled(curPath.empty() || !canWrite);
            const std::string clearBtnLabel =
                std::string(Orange::Editor::Theme::Icon::GetClose()) + "##clear";
            if (ImGui::SmallButton(clearBtnLabel.c_str()))
            {
                std::string newPath;  // empty
                writePath(newPath);
                host.cmdStack.Push(
                    std::make_unique<SetFieldValueCommand<std::string>>(
                        entity, fieldKey, oldPath, newPath, makeApply()));
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("清除字段（写入空路径）");
            }

            // Pick 按钮：把 Asset 浏览器当前选中的 asset path 写入字段。
            // 选中 path 必须非空 + 字段 set 可调 + 浏览器选中确实变了
            // （避免重复写同款 path 触发无意义命令栈条目）。
            const std::string& browserSel = host.assets.selectedAssetPath;
            const bool pickEnabled = !browserSel.empty()
                                  && browserSel != oldPath
                                  && canWrite;
            ImGui::SameLine();
            ImGui::BeginDisabled(!pickEnabled);
            const std::string pickBtnLabel =
                std::string(Orange::Editor::Theme::Icon::GetSearch()) + "##pick";
            if (ImGui::SmallButton(pickBtnLabel.c_str()))
            {
                std::string newPath = browserSel;
                writePath(newPath);
                host.cmdStack.Push(
                    std::make_unique<SetFieldValueCommand<std::string>>(
                        entity, fieldKey, oldPath, newPath, makeApply()));
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                if (browserSel.empty())
                {
                    ImGui::SetTooltip("Asset 浏览器无选中（点底部 Assets tab\n"
                                      "选一个 asset 后此按钮可点）");
                }
                else
                {
                    ImGui::SetTooltip("写入 Asset 浏览器当前选中:\n%s",
                                      browserSel.c_str());
                }
            }
            break;
        }
        case PropertyType::AssetRefArray:
        {
            // AssetRef 数组（首例：SubMeshMaterialsComponent.slots）。整段读
            // std::vector<std::string>（每元素一个资源相对路径）→ 逐 slot 一行
            // [Slot i] [短名/(none)] [×清除] + DnD 接收 → 整体回写 + Push
            // SetFieldValueCommand<std::vector<std::string>>。slot 数由 component
            // 决定（典型 = mesh 的 sub-mesh 数），本控件只编辑各 slot 指向的
            // 资源、不增删行。
            //
            // 与单 AssetRef case 的差异：accessor 的 out/in 指向 vector<string>；
            // EnsureMaterialInstance lazy 注册对每个非空 slot 路径执行；命令栈
            // 的 T = vector<string>（Merge 整体替换，一次 Undo 撤回本帧改动）。
            if (prop.assetRefGet == nullptr) { break; }
            std::vector<std::string> oldVal;
            prop.assetRefGet(component, host.assets, &oldVal);
            std::vector<std::string> newVal = oldVal;
            bool changed = false;

            const bool canWrite = (prop.assetRefSet != nullptr);
            const ImGuiStyle& s = ImGui::GetStyle();
            const float btnXW =
                ImGui::CalcTextSize(Orange::Editor::Theme::Icon::GetClose()).x
                + s.FramePadding.x * 2.0f;

            ImGui::BeginGroup();
            if (newVal.empty())
            {
                // slot 数为 0（component 挂了但 mesh 无 sub-mesh / 已清空）——
                // 给个提示行，避免空段看起来像 bug。
                ImGui::TextDisabled("（无 sub-mesh slot）");
            }
            for (std::size_t i = 0; i < newVal.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));

                const std::string& cur = newVal[i];
                std::string_view displayLabel;
                std::string      shortNameBuf;
                if (cur.empty())
                {
                    displayLabel = std::string_view{"(none)"};
                }
                else
                {
                    const auto slash = cur.find_last_of('/');
                    shortNameBuf = (slash == std::string::npos)
                        ? cur : cur.substr(slash + 1);
                    displayLabel = shortNameBuf;
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Slot %zu", i);
                ImGui::SameLine();

                // 短名 Selectable（DnD target 要求上一 item 有 ID；Selectable
                // 有，TextUnformatted 没有）。宽度 = 行内剩余 - 清除按钮宽。
                float nameW =
                    ImGui::GetContentRegionAvail().x - btnXW - s.ItemSpacing.x;
                if (nameW < 48.0f) { nameW = 48.0f; }
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      s.Colors[ImGuiCol_FrameBgHovered]);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                      s.Colors[ImGuiCol_FrameBgActive]);
                ImGui::Selectable(std::string(displayLabel).c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(nameW, 0));
                ImGui::PopStyleColor(3);
                if (!cur.empty() && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", cur.c_str());
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload("ORANGE_ASSET"))
                    {
                        const char* pData =
                            static_cast<const char*>(payload->Data);
                        std::string dropped{pData};
                        if (canWrite && dropped != newVal[i])
                        {
                            newVal[i] = std::move(dropped);
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 清除按钮：把本 slot 置空（渲染端回退到
                // RenderableComponent.materialInstance）。
                ImGui::SameLine();
                ImGui::BeginDisabled(cur.empty() || !canWrite);
                const std::string clearLabel =
                    std::string(Orange::Editor::Theme::Icon::GetClose()) + "##clr";
                if (ImGui::SmallButton(clearLabel.c_str()))
                {
                    newVal[i].clear();
                    changed = true;
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip("清除本 slot（回退到 Renderable 的默认材质）");
                }

                ImGui::PopID();
            }
            ImGui::EndGroup();

            if (changed && canWrite)
            {
                // Material slot：写入前 lazy 注册路径到 namedMaterialInstances，
                // 否则 setter 反查不到 ptr → slot 变 nullptr（与单 AssetRef
                // writePath 同款保证；刚导入 / 新建的 .material 不在表里）。
                if (prop.attribs.assetKind == AssetKind::Material)
                {
                    for (const auto& p : newVal)
                    {
                        if (!p.empty()) { (void)::EnsureMaterialInstance(host, p); }
                    }
                }
                prop.assetRefSet(component, host.assets, &newVal);
                host.cmdStack.Push(
                    std::make_unique<SetFieldValueCommand<std::vector<std::string>>>(
                        entity, fieldKey, oldVal, newVal,
                        MakeAssetRefFieldApply<std::vector<std::string>>(
                            &host, entity, &schema, prop.assetRefSet)));
            }
            break;
        }
        case PropertyType::PolygonVertices:
        {
            // Box2D PolygonDesc 顶点表编辑。读 → 用户改 → 整体回写 → Push
            // SetFieldValueCommand<PolygonDesc>。
            //
            // UI 结构（每一条 Inspector property "右列"内）：
            //   * 顶部 "Vertex Count: N / kMaxVertices"
            //   * 顶点表：每行 # / X / Y / [×] Remove 按钮
            //   * 底部 [+ Add Vertex]（顶点已满时 disabled）
            //
            // 标准 SchemaInspector property 走"左列 PropertyLabel + 右列控件"
            // 双列布局；顶点表整段挂在右列。本字段 prop.label 由外层
            // PropertyLabel 已经画出（"Vertices" 文字 + tooltip）；右列内不再
            // 重画 label。
            using Orange::Engine::Physics::PolygonDesc;

            PolygonDesc oldVal{};
            prop.get(component, &oldVal);
            PolygonDesc newVal = oldVal;
            bool        changed = false;

            ImGui::BeginGroup();
            ImGui::Text("Count: %u / %u",
                        static_cast<unsigned>(newVal.count),
                        static_cast<unsigned>(PolygonDesc::kMaxVertices));

            // 按 close 图标实际尺寸计算右侧预留：SmallButton 与 Button 不同，
            // 只吃 FramePadding.x 不吃 FramePadding.y。Inspector 右列是 stretch
            // 列，用户拖窄面板时 avail 会小到 < 100px——此时不能 clamp 到固定
            // 宽度，必须让 DragFloat2 主动收缩，按钮才能稳定贴右端。
            const ImGuiStyle& style = ImGui::GetStyle();
            const float closeBtnW =
                ImGui::CalcTextSize(Orange::Editor::Theme::Icon::GetClose()).x
                + style.FramePadding.x * 2.0f;
            for (std::uint32_t i = 0; i < newVal.count; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                float widgetW =
                    ImGui::GetContentRegionAvail().x - closeBtnW - style.ItemSpacing.x;
                if (widgetW < 40.0f) { widgetW = 40.0f; }
                ImGui::SetNextItemWidth(widgetW);
                glm::vec2 v = newVal.vertices[i];
                if (ImGui::DragFloat2("##xy", &v.x, 0.01f, 0.0f, 0.0f, "%.3f"))
                {
                    newVal.vertices[i] = v;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(Orange::Editor::Theme::Icon::GetClose()))
                {
                    for (std::uint32_t j = i; j + 1 < newVal.count; ++j)
                    {
                        newVal.vertices[j] = newVal.vertices[j + 1];
                    }
                    newVal.vertices[newVal.count - 1] = glm::vec2{0.0f};
                    --newVal.count;
                    changed = true;
                    ImGui::PopID();
                    break;  // 容器变更，跳出本帧循环
                }
                ImGui::PopID();
            }

            const bool canAdd = (newVal.count < PolygonDesc::kMaxVertices);
            ImGui::BeginDisabled(!canAdd);
            if (ImGui::SmallButton("+ Add Vertex"))
            {
                // 新顶点放在最后一个顶点附近（+0.5 X），方便用户后续微调；
                // 顶点数为 0 时放原点。
                glm::vec2 seed{0.0f};
                if (newVal.count > 0)
                {
                    seed = newVal.vertices[newVal.count - 1] + glm::vec2{0.5f, 0.0f};
                }
                newVal.vertices[newVal.count] = seed;
                ++newVal.count;
                changed = true;
            }
            ImGui::EndDisabled();
            if (!canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Polygon 顶点已达 Box2D 上限 %u",
                                  static_cast<unsigned>(PolygonDesc::kMaxVertices));
            }
            ImGui::EndGroup();

            if (changed)
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<PolygonDesc>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<PolygonDesc>(&host, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::EdgeChainVertices:
        {
            // 与 PolygonVertices case 同结构 + 多 isLoop checkbox。
            using Orange::Engine::Physics::EdgeChainDesc;

            EdgeChainDesc oldVal{};
            prop.get(component, &oldVal);
            EdgeChainDesc newVal = oldVal;
            bool          changed = false;

            ImGui::BeginGroup();
            ImGui::Text("Count: %u / %u",
                        static_cast<unsigned>(newVal.count),
                        static_cast<unsigned>(EdgeChainDesc::kMaxVertices));

            // 与 PolygonVertices case 同款右侧预留——按图标实际尺寸算，
            // 不再 clamp 到固定宽度，确保窄列下 Remove 按钮稳定贴右端。
            const ImGuiStyle& style = ImGui::GetStyle();
            const float closeBtnW =
                ImGui::CalcTextSize(Orange::Editor::Theme::Icon::GetClose()).x
                + style.FramePadding.x * 2.0f;
            for (std::uint32_t i = 0; i < newVal.count; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                float widgetW =
                    ImGui::GetContentRegionAvail().x - closeBtnW - style.ItemSpacing.x;
                if (widgetW < 40.0f) { widgetW = 40.0f; }
                ImGui::SetNextItemWidth(widgetW);
                glm::vec2 v = newVal.vertices[i];
                if (ImGui::DragFloat2("##xy", &v.x, 0.01f, 0.0f, 0.0f, "%.3f"))
                {
                    newVal.vertices[i] = v;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(Orange::Editor::Theme::Icon::GetClose()))
                {
                    for (std::uint32_t j = i; j + 1 < newVal.count; ++j)
                    {
                        newVal.vertices[j] = newVal.vertices[j + 1];
                    }
                    newVal.vertices[newVal.count - 1] = glm::vec2{0.0f};
                    --newVal.count;
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            const bool canAdd = (newVal.count < EdgeChainDesc::kMaxVertices);
            ImGui::BeginDisabled(!canAdd);
            if (ImGui::SmallButton("+ Add Vertex"))
            {
                glm::vec2 seed{0.0f};
                if (newVal.count > 0)
                {
                    seed = newVal.vertices[newVal.count - 1] + glm::vec2{0.5f, 0.0f};
                }
                newVal.vertices[newVal.count] = seed;
                ++newVal.count;
                changed = true;
            }
            ImGui::EndDisabled();
            if (!canAdd && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("EdgeChain 顶点已达上限 %u（超长链拆段处理）",
                                  static_cast<unsigned>(EdgeChainDesc::kMaxVertices));
            }

            bool loopVal = newVal.isLoop;
            if (ImGui::Checkbox("Loop", &loopVal))
            {
                newVal.isLoop = loopVal;
                changed = true;
            }
            ImGui::EndGroup();

            if (changed)
            {
                prop.set(component, &newVal);
                host.cmdStack.Push(std::make_unique<SetFieldValueCommand<EdgeChainDesc>>(
                    entity, fieldKey, oldVal, newVal,
                    MakeFieldApply<EdgeChainDesc>(&host, entity, &schema, prop.set)));
            }
            break;
        }
        case PropertyType::String:
        {
            std::string oldVal;
            prop.get(component, &oldVal);

            // readOnly 路径：右列仅显示动态 value（TextDisabled 灰显），左列
            // 的 prop.label 由 PropertyLabel 写过。不画 InputText、不 Push 命令。
            // 当前 readOnly 仅 String case 支持（c9 最小集）；其他 PropertyType
            // 上设 readOnly 暂被忽略走默认编辑控件。
            if (prop.attribs.readOnly)
            {
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
            if (ImGui::InputText("##v", buffer, sizeof(buffer)))
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

    // multi-edit 关组：见 CloseMultiEditGroupIfDone 注释——本文件开的 group 在
    // 拖动 / 编辑结束（全局无 active item）时收束。放在 switch 之后、PopID 之前，
    // 此刻字段控件已绘制完（DragFloatN 已处理本帧 mouse-release、ActiveId 归 0）。
    CloseMultiEditGroupIfDone(host);

    ImGui::PopID();

    // tooltip 已经在 PropertyLabel 内挂在左列 label 上 —— 控件本身不再做
    // hover 检查（控件在拖拽 / 编辑状态时 hover 行为会被打断，label hover
    // 更稳定）。
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
    // v0.6.5 c5：色带色按 schema.typeName 查表（"Transform" → 绿、
    // "Renderable" → 蓝、...）；未注册 typeName 走 GetDefault 浅灰 fallback。
    const ImVec4& bandColor =
        Orange::Editor::Theme::ComponentTypeBand::LookupByTypeName(schema.typeName);
    const bool open = ComponentHeaderLocal(
        schema.displayName ? schema.displayName : schema.typeName,
        &requestRemove, /*removable=*/schema.remove != nullptr,
        bandColor);

    if (open)
    {
        // 段顶 helper 文案 —— schema.helperText 非空时按行画 TextDisabled +
        // Bullet。位置：CollapsingHeader 展开后、plugin 调度 / properties 渲染
        // 之前。无论 plugin 是否接管，都先把 helper 暴露给 UI 用户；plugin 想
        // 完全自管段时 schema 不设 helperText 即可。
        if (schema.helperText != nullptr && schema.helperText[0] != '\0')
        {
            const char* p = schema.helperText;
            while (*p != '\0')
            {
                const char* lineEnd = p;
                while (*lineEnd != '\0' && *lineEnd != '\n') { ++lineEnd; }
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%.*s", static_cast<int>(lineEnd - p), p);
                ImGui::PopStyleColor();
                p = (*lineEnd == '\n') ? lineEnd + 1 : lineEnd;
            }
            ImGui::Spacing();
        }

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
            // ---- v0.4.5：所有 property 走两列 ImGui::Table 布局 ----
            //
            // 先一遍扫 schema 计算"本段最长 label 文本宽" maxLabelW（用 CalcText
            // Size，跳过 visibleIf 返回 false 的字段 + 纯 Group-only 占位 / readOnly-
            // 无-get 等不进 DrawProperty 的字段），传给 BeginPropertyTable 作为左列
            // 固定宽。每个 component schema 独立算自己的 maxLabelW —— 相邻段宽度可
            // 能不一致但 CollapsingHeader 视觉断开，自然。
            //
            // 段化：每条挂了 groupSeparator 的 prop 结束当前 table → SeparatorText →
            // 起一个新 table（labelColW 沿用）。这样 SeparatorText 可以占满整个
            // 段宽（不被两列分割），视觉上分组提示完整。
            float maxLabelW = 0.0f;
            for (const auto& p : schema.properties)
            {
                if (p.attribs.visibleIf != nullptr
                    && !p.attribs.visibleIf(component)) { continue; }
                if (p.get == nullptr) { continue; }  // Group-only / pure header
                if (p.set == nullptr && !p.attribs.readOnly) { continue; }
                if (p.label != nullptr)
                {
                    const float w = ImGui::CalcTextSize(p.label).x;
                    if (w > maxLabelW) { maxLabelW = w; }
                }
            }

            int  segIdx    = 0;
            bool tableOpen = false;
            auto endTable  = [&]() {
                if (tableOpen) {
                    Orange::Editor::Widgets::EndPropertyTable();
                    tableOpen = false;
                }
            };
            auto ensureTable = [&]() {
                if (!tableOpen)
                {
                    char id[96];
                    std::snprintf(id, sizeof(id), "##pt.%s.%d",
                                  schema.typeName ? schema.typeName : "?",
                                  segIdx);
                    tableOpen =
                        Orange::Editor::Widgets::BeginPropertyTable(id, maxLabelW);
                }
                return tableOpen;
            };

            for (const auto& prop : schema.properties)
            {
                if (prop.attribs.visibleIf != nullptr
                    && !prop.attribs.visibleIf(component))
                {
                    continue;
                }

                // GroupSeparator：结束当前 table，画 SeparatorText 占满整段宽，
                // 起一个新 table。允许"Group-only prop"（get/set 都为 nullptr）
                // 只为 SeparatorText 占位（Polygon / EdgeChain 等零字段段）。
                if (prop.attribs.groupSeparator != nullptr)
                {
                    endTable();
                    ImGui::SeparatorText(prop.attribs.groupSeparator);
                    ++segIdx;
                }

                // get / set 双源：AssetRef 走 assetRefGet/Set，其他 PropertyType
                // 走 get/set。任一来源非空即视为字段可显示 / 可写入。readOnly
                // 字段的写检查仍只看主路径（AssetRef readOnly 当前没用例）。
                const bool hasGet = (prop.get != nullptr)
                                 || (prop.assetRefGet != nullptr);
                const bool hasSet = (prop.set != nullptr)
                                 || (prop.assetRefSet != nullptr);
                if (!hasGet) { continue; }
                if (!hasSet && !prop.attribs.readOnly) { continue; }

                if (!ensureTable()) { continue; }

                // 字段 key："{type}.{prop}" —— 与既有 SetFieldValueCommand 风格
                // 一致；确保 coalesce 在同一字段连续编辑时合并成一条命令。
                std::string fieldKey;
                fieldKey.reserve(64);
                fieldKey.append(schema.typeName ? schema.typeName : "?");
                fieldKey.push_back('.');
                fieldKey.append(prop.name ? prop.name : "?");
                DrawProperty(host, entity, schema, prop, component, fieldKey);
            }
            endTable();
        }

        if (pActivePlugin != nullptr)
        {
            pActivePlugin->ParseEnd(host, entity, schema, component);
        }
    }

    if (requestRemove && schema.remove != nullptr)
    {
        // Transform Euler 缓存与 selectedEntity 联动；任意 component 被
        // Remove 都顺手 invalidate（Quat case 下一帧从最新 quat 重算 Euler）。
        host.selection.transformEulerCacheEntity =
            Orange::Engine::Entity::Invalid();

        if (schema.add != nullptr && schema.get != nullptr)
        {
            // 可 Undo：移除前把组件字段快照成 restorer；undo = schema.add 重建
            // 默认组件 + 跑 restorer 还原原值。component 是本段当前组件指针。
            // 不再 Clear cmdStack——remove 在栈顶，Undo 先命中它重建组件，之后
            // 才轮到更早的字段编辑命令（届时组件已在，schema.get 非空安全）。
            auto                       restorers = CaptureComponentState(host, schema, component);
            auto*                      pH        = &host;
            const ComponentSchema::RemoveFn removeFn = schema.remove;
            const ComponentSchema::AddFn    addFn    = schema.add;
            const ComponentSchema::GetFn    getFn    = schema.get;
            host.cmdStack.Push(std::make_unique<LambdaCommand>(
                "remove_component",
                [pH, entity, removeFn]() {
                    if (pH->scene.pWorld) { removeFn(*pH->scene.pWorld, entity); }
                    pH->selection.transformEulerCacheEntity =
                        Orange::Engine::Entity::Invalid();
                },
                [pH, entity, addFn, getFn, restorers]() {
                    addFn(*pH, entity);
                    if (pH->scene.pWorld) {
                        void* c = getFn(*pH->scene.pWorld, entity);
                        if (c != nullptr) {
                            for (const auto& r : restorers) { r(c); }
                        }
                    }
                    pH->selection.transformEulerCacheEntity =
                        Orange::Engine::Entity::Invalid();
                }));
        }
        else
        {
            // 无 add fn（不可重建，如 Name / Hierarchy / Animator）→ 保留原
            // 破坏性路径：直接 remove + Clear（栈内旧命令引用已失效 component
            // 槽位，无法安全 Undo）。
            schema.remove(*pWorld, entity);
            host.cmdStack.Clear();
        }
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
