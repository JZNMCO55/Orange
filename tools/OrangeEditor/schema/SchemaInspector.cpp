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
#include "../theme/EditorTheme.h"
#include "ComponentSchemaRegistry.h"

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
            if (ImGui::DragInt("##v", &newVal, prop.attribs.dragSpeed, minV, maxV))
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
            if (ImGui::DragScalar("##v", ImGuiDataType_U32, &newVal,
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
            if (ImGui::Checkbox("##v", &newVal))
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
            if (ImGui::DragFloat2("##v", &newVal.x,
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
