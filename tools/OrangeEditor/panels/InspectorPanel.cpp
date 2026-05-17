// Inspector 面板（DrawInspectorPanel + Add Component popup）。
//
// v0.2.5 commit 10 收尾：所有 component 列举 / +Add 菜单都走 Component
// SchemaRegistry 迭代，本 TU 内不再 mention 任何具体内置 component 类型。
// 与 EditorRenderLayer.cpp 主 TU 共享同一类声明 EditorRenderLayer。

#include "../EditorRenderLayer.h"

#include "../EditorWidgets.h"
#include "../MaterialFileIO.h"
#include "../schema/ComponentSchemaRegistry.h"
#include "../schema/SchemaInspector.h"
#include "../theme/EditorTheme.h"

#include <orange/engine/core/Serialization.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/scene/World.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// Inspector 入口：选中实体的 entity id + 所有"已挂着的 component"各起一段
// schema-driven 渲染 + 一个 +Add Component popup 按 schema registry 枚举。
//
// c10 起本 TU 不再 mention 任何具体内置 component 类型；所有组件清单 +
// 顺序 + UI 行为都从 ComponentSchemaRegistry 取。游戏侧自定义 component
// 通过同一 registry 注册即可自动出现在 Inspector + +Add 菜单——v0.3 game
// side schema 注册落地后无需改本 TU 一行。
// v0.5 c5：Material 子模式。当 Asset 浏览器选中一个 .material 文件时，
// Inspector 切到 material 编辑视图：MaterialTemplate Combo + uniforms /
// textures 调参 + Save 写回 .material 文件。
//
// 当前范围（GAP-2026-05-16-material-system-enumerate-and-instance-overrides
// G1 落地后）：
//   * Combo 项直接调 MaterialSystem::GetTemplateNames，覆盖内置 + 游戏侧
//     RegisterTemplate 注入的自定义模板
//   * uniform 调参 + 写盘逐步上线（见后续 commit C2/C3 的 enumerate API
//     + .material schema v1.1）
//   * 不支持创建新 .material（仅编辑已存在）/ 删除 / 重命名
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

    // Uniform 调参 UI：deferred 到后续 patch（独立 deliverable，不在本
    // GAP 范围）。schema v1.1 已支持持久化所有 SetUniform override，调参
    // UI 上线后此处接 BuildDataFromInstance + 各 uniform 控件即可。
    ImGui::Separator();
    ImGui::TextDisabled("Uniforms / Textures 调参 UI：deferred 到后续 patch");
    ImGui::TextDisabled("(.material schema v1.1 已支持持久化所有 SetUniform override)");

    // Save 按钮：以 v1.1 schema 写回（当前 Save 路径只动 templateName，
    // uniforms / textures 段写空——调参 UI 还没上线，无 override 可保存）。
    // dirty = 用户在 Combo 选的值 != 盘上原值。
    ImGui::Separator();
    const bool dirty = (host.assets.editingTemplateName != originalTemplate);
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save"))
    {
        ::Orange::Editor::Material::MaterialFileData data;
        data.templateName = host.assets.editingTemplateName;
        // uniforms / textures 留空——调参 UI 上线后此处改为
        // BuildDataFromInstance(currentInstance, editingTemplateName)。
        ::Orange::Editor::Material::WriteMaterialFile(materialPath, data);
        // 内存 MaterialInstance 不在此重新 CreateInstance —— 那会让 Render
        // able.materialInstance 字段持有的旧指针悬挂。完整刷新路径要走
        // namedMaterialInstances 重建 + 所有 Renderable 字段重定向，超出
        // c5 范围。当前 Save 只动盘上文件，运行时直到重启编辑器才看到效果。
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
        ImGui::TextDisabled("注：当前会话的运行时 MaterialInstance 未刷新；");
        ImGui::TextDisabled("重启 OrangeEditor 可看到新 template 生效。");
        if (ImGui::Button("OK")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

// 判断 selectedAssetPath 是否是 .material 文件路径。
bool IsMaterialAssetSelected(const std::string& path)
{
    if (path.size() < 9) { return false; }  // ".material" = 9 chars
    return path.compare(path.size() - 9, 9, ".material") == 0;
}

}  // anonymous namespace

void EditorRenderLayer::DrawInspectorPanel()
{
    ImGui::Begin("Inspector");

    // v0.5 c5：Material 子模式优先级 —— 当 Asset 浏览器选中 .material 文
    // 件时，Inspector 切到 material 编辑视图。实体选中仍保留在 selection
    // 内，用户在 Asset 浏览器选别的文件或清空选中后自动切回实体 Inspector。
    if (IsMaterialAssetSelected(mHost.assets.selectedAssetPath))
    {
        DrawMaterialSubMode(mHost, mHost.assets.selectedAssetPath);
        ImGui::End();
        return;
    }

    if (mHost.scene.pWorld == nullptr || !mHost.selection.selectedEntity.IsValid()) {
        ImGui::TextDisabled("(select an entity)");
        ImGui::End();
        return;
    }
    // Entity::IsValid() 只检查是否为哨兵 null 值；Undo 可能已销毁该实体但
    // 未清掉句柄。World::IsValid 走 registry.valid()，可正确甄别死实体。
    if (!mHost.scene.pWorld->IsValid(mHost.selection.selectedEntity)) {
        mHost.selection.selectedEntity            = Orange::Engine::Entity::Invalid();
        mHost.selection.transformEulerCacheEntity = Orange::Engine::Entity::Invalid();
        ImGui::TextDisabled("(select an entity)");
        ImGui::End();
        return;
    }

    const Orange::Engine::Entity e = mHost.selection.selectedEntity;
    ImGui::Text("Entity #%u",
                static_cast<unsigned>(static_cast<std::uint32_t>(e.Value())));
    ImGui::Separator();

    // Play / Paused 期间所有 component 字段只读（灰显但可见）。
    const bool canEdit = (mHost.scene.playState == PlayState::Edit);
    if (!canEdit) {
        ImGui::TextDisabled("[ Read-only in Play / Paused ]");
        ImGui::Separator();
    }
    ImGui::BeginDisabled(!canEdit);

    // 所有 schema 段按注册顺序逐段渲染（schema.has 内部已 guard 未挂的 entity，
    // 不会画空段）。注册顺序见 schema/RegisterBuiltinSchemas.cpp，与 v0.1
    // 期 Inspector 内 component header 顺序一致：Name → Transform → Hierarchy
    // → DirectionalLight → Renderable → RigidBody → Collider → ParticleEmitter
    // → Animator。
    Orange::Editor::Schema::DrawEntityViaSchemas(mHost, e);

    // ---- + Add Component -------------------------------------------------
    // 枚举 ComponentSchemaRegistry 内所有挂了 `.Addable()` / `.AddableWith()`
    // 的 schema 项；过滤掉已挂在本实体上的 component。schema.add(host, entity)
    // 完成实际挂载——默认 Addable 走 `world.AddComponent<C>(e, C{})`，
    // AddableWith 走调用方自定义 lambda（典型：Renderable 预绑 cubeMesh +
    // defaultMaterial）。
    //
    // AddComponent 是破坏性操作（从此刻起历史才有意义），清掉 undo 历史
    // 防止旧命令对组件布局做出错误假设。
    //
    // 跳过的 component（schema 未挂 Addable）：
    //   * Name / Hierarchy —— 由 entity 创建路径 / Entity Tree DnD 自动管理
    //   * Animator         —— IAnimator 抽象类，需要具体子类实例
    auto& schemaReg = Orange::Editor::Schema::ComponentSchemaRegistry::Instance();
    ImGui::Separator();
    const std::string addComponentLabel =
        std::string(Orange::Editor::Theme::Icon::GetAdd()) + " Add Component";
    if (ImGui::Button(addComponentLabel.c_str())) {
        ImGui::OpenPopup("##add_component");
    }
    if (ImGui::BeginPopup("##add_component")) {
        auto* pWorld = mHost.scene.pWorld.get();
        if (pWorld != nullptr)
        {
            for (const auto& schema : schemaReg.All()) {
                if (schema.add == nullptr) { continue; }        // 未挂 Addable
                if (schema.has != nullptr && schema.has(*pWorld, e)) {
                    continue;                                    // 已挂
                }
                const char* label = (schema.displayName != nullptr)
                                  ? schema.displayName
                                  : (schema.typeName ? schema.typeName : "?");
                if (ImGui::MenuItem(label)) {
                    schema.add(mHost, e);
                    mHost.cmdStack.Clear();
                }
            }
        }
        ImGui::EndPopup();
    }

    ImGui::EndDisabled();
    ImGui::End();
}

// ---- v0.2.5 schema-driven 迁移历史索引 -------------------------------
//
// v0.2.5 commit 3 ~ 10 把 EditorRenderLayer 的 9 个 DrawInspectorXxx 成员
// 函数 + 静态 ComponentHeader helper 全数清除，每个 component 段都由
// schema/RegisterBuiltinSchemas.cpp 内的 Register*ComponentSchema() 注册
// 驱动；ComponentHeaderLocal 在 schema/SchemaInspector.cpp 内部承担同等
// 角色的 CollapsingHeader 包装。
//
// 旧 DrawInspectorXxx → 迁移 commit 对照：

// DrawInspectorName / DrawInspectorTransform / DrawInspectorHierarchy 已删除
// —— v0.2.5 commit 5 起：
//   * NameComponent       走 PropertyType::String 通用路径（schema/Register
//                         BuiltinSchemas.cpp 内 RegisterNameComponentSchema）。
//                         RenameCommand 在 schema 路径下被 SetFieldValueCommand
//                         <std::string> 取代——CommandStack::Push 内 fieldKey
//                         "Name.name" + 同 entity 触发 Merge，等价 RenameCommand
//                         的连续改名 coalesce 行为
//   * TransformComponent  position / scale 走 Vec3 通用路径；rotation 走 Quat
//                         case（SchemaInspector.cpp）的 Euler-cache 混合路径，
//                         缓存仍在 EditorSelection.transformEulerCache，apply
//                         lambda 内 invalidate cache
//   * HierarchyComponent  4 个 Entity 字段走 PropertyType::EntityRef 只读路径
//
// v0.1 期 Hierarchy 段的 ImGui::TextDisabled "(edit by drag-drop in Entity
// Tree)" 提示本期接受视觉降级；后续 v0.3 IEditorInspectorPlugin 落地时还原。

// DrawInspectorDirectionalLight 已删除 —— v0.2.5 commit 3 起 DirectionalLight
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp）。"Normalize
// Direction" 一键按钮目前未在 schema 路径表达：schema 系统当前只支持
// 字段 read/write，没有"action button"概念。后续 commit 在 schema 系统
// 引入 ActionButton attribute 或独立 IEditorInspectorPlugin 路径后再
// 还原该 helper（短期可接受退化——direction 在 ImGui DragFloat3 拖动
// 时大概率仍是单位向量附近的值；Pipeline 着色阶段按需 normalize 保底）。

// DrawInspectorRigidBody 已删除 —— v0.2.5 commit 4 起 RigidBodyComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 RegisterRigid
// BodyComponentSchema()）。`handle` 字段（PhysicsWorld::AddBody 反写的运
// 行时 BodyHandle）暂未在 schema 内显示——schema 系统当前没有 "DisplayOnly /
// read-only" 字段标记，本期接受这一行 TextDisabled 调试值的视觉降级，等
// 后续 commit 引入 read-only display attribute 后再补回。Combo 控件能力
// 由本 commit 同步引入的 PropertyType::Enum 提供，BodyType enum 项名表见
// schema 注册代码。

// DrawInspectorCollider 已删除 —— v0.2.5 commit 8 起 ColliderComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 Register
// ColliderComponentSchema()）。
//
// shape 是 std::variant<CircleDesc, BoxDesc, PolygonDesc, EdgeChainDesc>；
// 本 commit 同步引入 PropertyAttributes::visibleIf + Builder::VisibleIf /
// Builder::Group 两个 schema 入口，把 v0.1 hardcode 内"按 alternative 显
// 示不同字段"的行为表达成"每个 shape 段挂 visibleIf(holds_alternative<X>)"。
// shape 类型切换控件**不**引入（v0.1 deliberately not implemented，避免
// 切换 alternative 时重置数据；保留给后续 collider 专用 UI / v0.4 Gizmo）。
//
// 视觉降级（vs v0.1）：
//   * Polygon / EdgeChain 段不再显示动态 vertex count / loop 状态
//     （v0.1 `Shape: Polygon (%u verts)` / `Shape: EdgeChain (... loop=...)`）
//   * 字段顺序由 "shape 段在前 / 通用字段在后" 改为 "通用字段在前 / shape
//     段在后" —— schema 是线性顺序，Polygon / EdgeChain 的零字段段放在中
//     间会让通用字段视觉被挤；颠倒顺序让 shape 段总在 component 段末尾
// 两条都属 informational 降级，运行时行为与 v0.1 一致。

// DrawInspectorAnimator 已删除 —— v0.2.5 commit 9 起 AnimatorComponent
// 走 schema-driven 渲染（schema/RegisterBuiltinSchemas.cpp 内 Register
// AnimatorComponentSchema()）。
//
// v0.1 hardcode 显示两行：
//   1. `Animator (runtime) : <ptr>` —— 原始 IAnimator* 指针地址
//   2. `(animator backend editing — later task)` TextDisabled 占位
//
// c9 改显 backend name 字符串（"skeletal_dragonbones" / "procedural" 等）。
// 同步引入 PropertyAttributes::readOnly + Builder::ReadOnly() 让 String
// 字段走"Text + SameLine + TextDisabled"展示路径，不暴露 InputText。
//
// 信息变更（vs v0.1）：
//   * 指针地址  → backend name（更可读，信息量提升）
//   * "later task" 占位文本 → 不再显示（informational 删除）
// 行为变更：无（v0.1 也无任何编辑能力）。
//
// readOnly attribute 同时为后续还原 c4 RigidBody.handle / c7 Renderable
// mesh handle / MaterialInstance ptr 的调试显示打下基础——但那些字段还
// 需要专门的 PropertyType（AssetHandle / BodyHandle 等），等专门 commit
// 解决，本 commit 不顺手做。
