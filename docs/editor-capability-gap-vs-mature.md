<!-- 本文档由 Claude Code workflow（orange-editor-whole-vs-mature，11 子代理，2026-05-29）自动综合生成。证据列 file:line 取自当时代码审计，随代码演进可能漂移，引用前请复核。场景层级编辑见独立报告。 -->

# OrangeEditor 全编辑器能力 Gap 报告 vs 成熟编辑器

> **参照系说明**：本文成熟编辑器对照来自 in-engine 编辑器的通用模式知识（Unity / Unreal / Godot / LumixEngine 架构记忆），**非对 Lumix 源码逐行核对**；个别实现细节（如 prefab override 传播粒度、IK/root-motion 完备度）若要采纳应在落地前核实源码。
> **范围声明**：**场景层级编辑（Hierarchy / EntityTree / 多选 / DnD reparent）已在独立报告覆盖，本文不重复**，仅在跨子系统优先级处引用其结论。
> **证据纪律**：表中"当前状态"列的证据均取自八子系统现状审计（file:line）。现状审计未覆盖之处一律标"未评估"，不臆造。GUI 验证列依据"凡涉及 ImGui 交互/手感/视觉/焦点竞争一律 Y"原则如实标注——本项目刚连续踩 3 个交互层 bug（DnD / 双击 / Ctrl-toggle，全靠真人点拖暴露），保守从严。

---

## 1. 一句话总览

OrangeEditor 已是一个**架构纪律扎实、数据通路（schema-first Inspector / DCC import 4 件套 / 单文件+per-layer 场景序列化 / 命令栈 coalesce / dock 持久化）成熟的"数据编辑器"**，水位接近成熟 in-engine 编辑器的**骨架基线**；但相对成熟编辑器，**主要差六大类**：

1. **交互式直接操作的深度**——gizmo 无 snap / 无 local-world 切换 / 无多选群组变换、plugin gizmo 全是纯装饰不可拖、viewport 无框选/无修饰键多选；
2. **视觉反馈闭环**——全编辑器零缩略图/预览球、零 RT/G-buffer/wireframe debug view（依赖 OrangeRender 跨仓能力）；
3. **可复用性资产**——无 prefab、无多场景/子场景/scene 引用；
4. **运行时玩法闭环**——Play 仅 tick 引擎内置子系统，无法加载运行游戏代码（无脚本运行时 / 无 dll 热加载 / 无 workspace 项目模型）；
5. **时序/曲线编辑**——零时间轴/dopesheet/曲线编辑器，动画与粒子均退化为"start→end 线性"或纯字段；
6. **资产生命周期工具**——无搜索/过滤、无依赖追踪（谁引用了我）、无删除/重命名、无 file watcher 自动 reimport、无编辑器 autosave 接线。

其中 (2)(4) 的大头**需引擎/渲染器侧先提供能力**，属跨仓多 session；(1)(3)(5)(6) 多数可在编辑器单仓内推进，但 (1)(5) 的手感必须作者 GUI dogfood。

---

## 1.5 落地进度（2026-05-29 sweep，本地未 push 待 dogfood）

> 本节记录依本报告自主推进的落地状态。**下表所有 ✅ 项均本地 commit 未 push**，
> 逐项交互验证清单见 `build/dogfood-checklist.md`（gitignored）。dogfood 通过后
> push。证据纪律：交互/手感件标 ✅ = 已编译+lint+（可测核心）单测，**手感仍待 dogfood**。

**已落地（✅，按 §3 优先级）**：
- **P0 全清**：材质 dirty 追踪（facet-1 关窗拦截 + facet-2 持久 uniformDirty）/ autosave 接线+崩溃恢复 / Undo-redo 动作名 / **Gizmo snap 三轴**（translate 网格 + rotate 角 + scale 步，可测核心 `editor_math_util_test`）/ **Gizmo Local/World 切换** / **视口 Ctrl 多选 + 相机 MMB pan**。
- **P1 单仓件**：资产浏览器**搜索 + 类型过滤**（谓词 `editor_text_util_test`）/ **Layer count + 重命名 + 排序**（reorder 引擎 `WorldPartition::MoveLayer` + `scene_world_partition_test`）/ **多选群组变换 translate+rotate+scale 三模式**（pivot 数学 `editor_group_transform_test`，pivot = primary 位置，单选零回归）/ **Inspector multi-edit**（读侧共有组件求交摘要 + 写侧 `BroadcastFieldToSelection<T>` 把共有字段编辑广播到全部选中 + 仅多选才开的拖动分组收成一次 Undo；9 编辑型 case 覆盖，单选零回归；AssetRef/EntityRef 不广播）。
- **可测核心硬化**：`EditorGizmoMath`（三 gizmo 反投影/最近点/ray-plane/屏幕投影地基）补 `editor_gizmo_math_test`。
- **Layer DnD entity→layer**（§2.7 Layer 子系统收尾）：拖 Entity Tree 实体落 layer 名 → SetLayerOf 改归属，drop target 锚名字 Text 避 anchor 坑，帧末 cmdStack 可 Undo。Layer 四件套 count/rename/reorder/DnD 全闭环。
- **RemoveComponent 可 Undo**（§2.4/P1 破坏性 undo 的组件级）：`CaptureComponentState` 移除前按 schema 字段快照值，undo = schema.add 重建 + restorer 还原原值；可重建组件不再 Clear cmdStack。删实体级（需 EnTT id 稳定 / prefab P2）未做。
- **资产引用只读扫描**（§2.2 依赖追踪只读半场）：`EditorAssetReferences::FindAssetReferences` 全实体 × AssetRef-schema 扫引用，资产浏览器"referenced by N"+ hover 列引用方。
- **资产 rename**（§2.2 写半场可逆核心）：右键 context menu 内联 rename + 校验 + 引用计数；`RemapAssetReferences` 重扫改引用；cmdStack 可 undo（fs::rename 文件+.meta+Remap，顺序：先 rename 再 Remap）。仅 handle 类（mesh/texture/sound）；material 禁用（ptr 身份）；触碰真实文件但可逆。
- **Quick-Win**：Save-As-New 材质 / view-toggle 持久化 / 相机书签持久化 / Console 大小写搜索 + 时间戳列 / 未注册 component warning / Play snapshot 唯一名。#5 HDR-color、#6 RegisterComponentSchema 经 verify-gate 判 speculative / stale-doc **不做**。

**剩余（focused-session + 增量 dogfood，不盲堆到本已 25-deep 未验证批）**：
- **破坏性 undo 的删实体级** —— 删整个实体需 EnTT id 稳定引用基建（绑 GUID/prefab P2，`SceneSerialization` 仅 whole-world）。组件级 RemoveComponent-undo 已落地（见上）。
- **资产 delete** —— 依赖扫描 + rename（可逆写）已落地（见上 §1.5）；剩余仅 delete。delete 真不可逆（fs 删无备份）→ 需设计决策：软删除（move 到 trash 可 undo）+ 清引用并捕获恢复（Remap 不适用，清空后扫不到，需 capture 原 refs 列表，扩 AssetReference 带 schema/prop 或 re-resolve）+ 强确认显示牵连。是需慎重拍板的 focused session 件。
- **§3 P2 全部** —— 跨仓（OrangeRender / 引擎序列化前置），ADR-009 禁同 session 双向。

---

## 2. 按子系统分节的 gap 表

> 工作量口径：**S** = 1-commit 级（对标 warning chip / sibling reorder）；**M** = 数 commit / 单 session 可收口；**L** = 跨多 session 或需引擎侧前置能力。

### 2.1 变换 Gizmo + 视口操作

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| 平移/旋转/缩放单轴 gizmo | 已实现（`EditorTranslateGizmo.cpp:103` / `EditorRotateGizmo.cpp:133` / `EditorScaleGizmo.cpp:106`） | 同（通用基线） | — | — | Y | N |
| 平移平面/屏幕中心 handle | 缺失（未读到 XY/XZ/YZ 四方块与中心拖动） | Unity/Lumix 平面+屏幕 handle | M | hit-test 与单轴竞争 | **Y** | N（求交内核可单测，命中分流必 dogfood） |
| Local vs World 切换 | 缺失（Translate/Rotate 写死 world，Scale 写死 local，无 X 键 toggle，`EditorGizmoState` 无枚举） | World/Local/Parent 切换 | S-M | rotation 乘法方向易错 | **Y** | Y（轴向量计算可单测） |
| Snap（grid/angle/step） | 缺失（gizmo 内 grep `snap` 0 命中；`EditorSettings.h` 无 snap 字段） | grid/angle/scale snap + Ctrl 临时启停 | M | 量化点与连续拖动手感 | **Y** | Y（量化函数纯逻辑可单测） |
| 多选 pivot / 群组变换 | 缺失（三 gizmo 只读 primary，`additionalSelectedEntities` 不参与变换，`ScenePanel.cpp:260,272`） | 选集中心 pivot + 保相对偏移 | L | 旋转保偏移数学 + 多 undo 合并 | **Y** | Y（pivot/相对变换数学可单测） |
| 组件 plugin gizmo 交互（5+1 个） | 部分=纯装饰（`ScenePanel.cpp:402` 只调 Draw 不调 HitTest；`GizmoContext.h:32-47` 无 mouseRay/handle 槽） | 灯光/锥体 handle 可拖 | L | 需打通 HitTest 整条路径 + 逐 plugin override | **Y** | N（hit-test 内核可单测，接管手感必 dogfood） |
| 视口单击 picking | 已实现 AABB 粒度（`EditorPicking.cpp:129`；仅可见 mesh，灯光/相机不可点选 `:168`） | 三角面精确 + 全 entity 可选 | M | 细长/旋转物误选 | **Y** | Y（ray-AABB/ray-tri 求交可单测） |
| 视口 Ctrl/Shift 多选 + 框选 marquee | 缺失（`ScenePanel.cpp:472` 直接覆盖不读修饰键；marquee grep 0） | 修饰键多选 + 橡皮筋框选 | M | 焦点/拖拽阈值竞争（本项目踩过 Ctrl-toggle bug） | **Y** | N（框选求交可单测，修饰键分流必 dogfood） |
| 相机 pan（MMB/Alt） | 缺失（`UpdateEditorCameraFromInput` 无平移） | MMB/Alt 平移 pivot | S | 与 orbit gate 竞争 | **Y** | N |
| 相机 fly / WASD | 缺失（无飞行模式枚举） | RMB+WASD 飞行 | M | 输入捕获 + 模式切换 | **Y** | N |
| 相机 saved view / 持久化 | 缺失（`EditorCameraState.h:9` 标"未来"） | named view + 持久化 | S-M | 低 | **Y**（验证恢复正确） | Y（序列化往返可单测） |
| Grid/Sky/DebugDraw/Colliders toggle | 已实现但不持久化（`ScenePanel.cpp:83/91/99/108`，file-static `:47`） | 同 + 持久化 | S | 低 | **Y** | Y（持久化字段可单测） |
| View Mode（Persp/2D Lock） | 缺失=disabled 占位（`ScenePanel.cpp:137`，依赖相机 2D 锁定） | 2D/3D 视图锁 | M | 依赖 `GAP-2026-05-15` | **Y** | N |
| Shading（wireframe/shaded+wire） | 缺失=disabled 占位（`ScenePanel.cpp:151`；render 公共头无 `PolygonMode/FillMode`） | wireframe view mode | L | **需 OrangeRender wireframe pass（跨仓）** | **Y** | N |
| Unlit/Normals/Overdraw/Albedo debug view | 缺失（render 公共头 0 命中） | G-buffer 通道可视化 | L | **需引擎渲染侧能力（跨仓）** | **Y** | N |
| RT / G-buffer 缩略图可视化 | 缺失（grep 0 命中） | 中间 RT 预览 | L | **需引擎 offscreen RT 暴露（跨仓）** | **Y** | N |
| Shadow cascade tint 调试 | 已实现（`RenderSettingsPanel.cpp:66`） | 同 | — | — | Y | N |

### 2.2 资产管线 + 浏览器

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| Texture import（PNG/JPG/TGA/HDR） | 已实现（`ImportDispatcher.cpp:56-61`；`TextureLoader.cpp`） | 同 | — | sRGB flag 未写（`TextureLoader.cpp:135`） | N | Y |
| Mesh import（OBJ / glTF / GLB） | 已实现（`ObjImporter.cpp:79` / `GltfImporter.cpp:134`，PBR material 解析） | 同 | — | glTF 多 material/内嵌贴图/外部 .bin 缺口 | N | Y |
| FBX / DAE / USD import | 缺失（`ImportDispatcher.cpp:54-71` 返回 Unsupported；`engine-known-gaps.md:701` 延后） | FBX（OpenFBX） | L | vendor importer 模块 | N | Y |
| EXR / KTX / DDS / BC7 | 缺失（仅标 icon 无 importer） | 压缩纹理管线 | L | 性能 milestone | N | Y |
| DCC 4 件套（vendor/二进制+copy/.meta/registry） | 已实现，.meta 部分（`importParams{}` 始终不写 `MetaSidecar.cpp:183-191`；mesh 复用 TextureMetaV1） | 完整 .meta sidecar | M | JsonWriter 无"建空 object"入口 | N | Y |
| 文件夹树 + 文件列表 + 面包屑 | 已实现（`EditorRenderLayer.cpp:1310-1352/1366-1611/1835-1849`） | 同 | — | — | **Y** | N |
| 类型 icon | 部分=ASCII 文本前缀（`EditorRenderLayer.cpp:1393-1404`，非 icon font 非缩略图） | icon font + 缩略图 | S-M | 视觉 | **Y** | N |
| 缩略图 / 预览（3D/材质/纹理） | 缺失（`EditorRenderLayer.cpp:1824` 决策不引入；G2 登记未做 `engine-known-gaps.md:617-619`） | 异步离屏渲染缩略图 + 磁盘 cache | L | **需 OrangeRender offscreen RT（跨仓）** | **Y** | N（bake 逻辑部分可单测） |
| 搜索 + 类型/标签过滤 | 缺失（Assets 列表无 search/filter 控件） | 搜索 + 过滤 | S-M | 低 | **Y** | Y（过滤谓词可单测） |
| 创建资产 | 部分=仅 Create→Material（`EditorRenderLayer.cpp:1656-1766`；Create→Scene/Folder 延后） | Create 多类型 | S-M | modal 焦点 | **Y** | N |
| 删除 / 重命名（自动修引用） | 缺失（`EditorRenderLayer.cpp:1827` 注释明示不支持） | 删/重命名 + 引用修复 | L | **依赖依赖图能力** | **Y** | Y（引用重写可单测） |
| Reimport（右键/Inspector/hash 短路） | 已实现（`EditorRenderLayer.cpp:1538-1571` / `ImportMetaAssetInspectorPlugin.cpp:90` / FNV-1a `MetaSidecar.cpp:29-67`） | 同 | — | 源失效无 fallback search | **Y**（右键路径） | Y（hash 短路可单测） |
| File watcher 自动 reimport | 缺失（`engine-known-gaps.md:701` 延后） | watcher + live reload | M-L | 平台文件监控 | N | Y（watcher 回调可单测） |
| 资产依赖追踪（谁引用了我） | 缺失（`AssetRegistry.h` 无 reverse-lookup；.meta 无 dependency 字段） | 引用图 + 反查 | L | 全局依赖图基础设施 | N | Y（图查询可单测） |
| OS 文件 drag-drop import | 已实现（`main.cpp:794-802` → `ApplyPendingImports`） | 同 | — | 文件夹/批量未展开 | **Y** | N（drain 逻辑可单测） |
| 文件夹/多文件批量 drop | 部分=多文件循环但文件夹不递归（`engine-known-gaps.md:683-685`） | 递归展开 | S-M | 低 | **Y** | Y（递归枚举可单测） |
| Texture 编译产物（mipmap/压缩/ORTX） | 部分=仅 copy 原图（`ImportDispatcher.cpp:131`，运行时生 mipmap） | 离线 cook | L | 性能 milestone | N | Y |
| AssetRegistry 跨进程序列化 | 缺失（`EditorAssetContext.h:13-16` 注释；`GAP-2026-05-16`） | handle 持久化 | L | 序列化语义 | N | Y |

### 2.3 材质 + shader 编辑

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| `.material` 读写（v1.1 schema） | 已实现（`MaterialFileIO.cpp:63-276`） | 文本材质资产 | — | texture override 依赖 PathOf 反查 | N | Y |
| MaterialInstance override 体系 | 已实现（`MaterialInstance.h:43-123`） | 母材质 + 实例覆盖 | — | Set 不匹配 silent no-op | N | Y |
| Shader template auto-scan（.template.json） | 已实现（`MaterialSystem.cpp:368-432`，8 内置） | 数据驱动 shader 注册 | — | 仅收 SPV 不编译 GLSL | N | Y |
| baseline 15 template 库 | 部分=8/15（53%，`engine-known-gaps.md:1355-1369`，缺 7 因 Pipeline/Render 端能力） | 完整模板库 | L | **多数缺项需 OrangeRender（跨仓）** | N | Y |
| Inspector 材质子模式 + 数据驱动 widget | 已实现（`MaterialAssetInspectorPlugin.cpp:62-517`） | 参数面板 | — | 切 template 需重启重建 instance | **Y** | N（widget 元数据 parse 可单测） |
| Create Material 入口 | 已实现（`EditorRenderLayer.cpp:1656-1766`） | 同 | — | overwrite modal | **Y** | N |
| "Save As New Material" | 缺失（仅登记可选 `engine-known-gaps.md:621-624`） | 另存变体 | S | 低 | **Y** | N |
| 材质参数旁路命令栈/dirty | 已实现=旁路（`MaterialAssetInspectorPlugin.cpp:454-474` 直接写盘，`scene.dirty` 不置位） | 改材质进 undo + dirty | M | **关编辑器不提示材质未保存**（真陷阱） | **Y** | Y（命令往返可单测） |
| 材质预览球 / 缩略图 | 缺失（trim 到 v1.4.0；需 mini-pipeline 500-800 LOC + offscreen RT `editor-roadmap.md:902`） | 预览球实时渲染 | L | **需 OrangeRender offscreen RT（跨仓）** | **Y** | N |
| Shader 热重载 / runtime GLSL 编译 | 缺失（`MaterialSystem.h:20`；依赖 glslang+UBO `engine-known-gaps.md:1313-1321`） | 改 shader 即时生效 | L | **需 vendor glslang + Material UBO（跨仓）** | **Y** | N |
| 节点式材质图（shader graph） | 缺失（`engine-known-gaps.md:1323-1328` Phase 11+ 或永不） | node-graph 材质 | L | 数月级，不主动开工 | **Y** | N |

### 2.4 Inspector / 属性 / 组件

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| Schema-first 属性自动绘制 | 已实现（`SchemaInspector.cpp:1050-1057`，registry 遍历） | 反射属性面板 | — | — | **Y** | Y（schema 注册/类型擦除可单测） |
| 标量/Vec/Color/Quat/Enum/String/Polygon 控件 | 已实现（`SchemaInspector.cpp:195-868`） | 同 | — | — | **Y** | N |
| AssetRef DnD 写入 | 已实现（`SchemaInspector.cpp:457-644`，进命令栈） | 同（本项目踩过 DnD bug） | — | DnD payload 焦点 | **Y** | N（dispatch 可单测） |
| EntityRef 写入控件 | 部分=只读 `#id`（`SchemaInspector.cpp:432-455`，DnD 写回仅注释） | DnD entity 引用 | M | **依赖 EntityGUID 稳定引用** | **Y** | N |
| 数值精确输入 + 增量拖拽 | 已实现（DragFloat/DragInt 系列） | 同 | — | — | **Y** | N |
| 属性分组 / SeparatorText | 部分=只有分隔线无折叠（`engine-known-gaps.md:152`） | collapsible foldout 子段 | S-M | 低 | **Y** | N（API 设计可单测） |
| Multi-edit 共有属性写所有选中 | 部分=仅 banner（`InspectorPanel.cpp:72-82`） | 异构多选共有属性写回 | L | 命令栈批量合并 | **Y** | Y（共有属性求交 + 批写可单测） |
| 属性改动 undo 集成（coalesce/replay 安全） | 已实现（`SetFieldValueCommand.h:22-61`，fieldKey coalesce） | 同 | — | — | **Y**（合并手感） | Y（coalesce/replay 可单测） |
| Inspector / Asset / Gizmo plugin 扩展点 | 已实现（`IEditorInspectorPlugin.h` / `IEditorAssetInspectorPlugin`，5+ 注册） | 同 | — | 无 property-level 拦截 / 无热重载 | **Y** | N |
| 游戏侧 schema 注册封装 API | 部分=文档承诺的 `RegisterComponentSchema<T>` 不存在，须裸用 builder（`HealthComponent.cpp:69-79`） | 一行注册扩展点 | S | 低 | N | Y |
| Add/Remove Component | 已实现但不可 undo（`InspectorPanel.cpp:160` / `SchemaInspector.cpp:1032`，执行即 `cmdStack.Clear()`） | 可 undo 的增删 | M | 解锁需完整序列化 | **Y** | Y（add/remove 序列化往返可单测） |
| HDR 颜色编辑 | 缺失（`SchemaInspector.cpp:286-289` 仅 LDR，无 HDR flag） | HDR ColorEdit | S | 低 | **Y** | N |
| Mat4/AssetHandle/BodyHandle 类型 | 缺失/只读（多处 deferred） | 完整类型覆盖 | M | 按需触发 | **Y** | Y |
| "action button"（如 Normalize Direction） | 缺失（schema 只表达字段 r/w，`InspectorPanel.cpp:202-205`） | 一键操作按钮 | S-M | 低 | **Y** | N |

### 2.5 Play-in-Editor / 运行时模拟

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| Edit/Play/Pause 三态状态机 | 已实现（`EditorSceneContext.h:46-62`；帧末统一 apply `EditorRenderLayer.cpp:1018`） | 同 | — | — | **Y** | Y（迁移守卫可单测） |
| Play 期 physics/vfx/animator/audio tick | 已实现（`EditorRenderLayer.cpp:191-258, 1056-1129`） | 同 | — | vfx 依赖 Pipeline 已 init；audio pitch/loop 无效 | **Y** | N（tick 接线难纯逻辑测） |
| Play→Stop 序列化快照&还原 | 已实现（`EditorRenderLayer.cpp:1031-1051/1152-1197`） | 同（序列化往返还原） | — | snapshot 固定文件名（多实例冲突未确认） | **Y** | Y（save/load 往返可单测） |
| Play 期编辑禁用 | 已实现（Inspector/Tree/gizmo/Undo 全 gate） | 同 | — | — | **Y** | N |
| 相机/输入 Edit↔Play 切换 | 缺失（`EditorCameraControl.h` 无 PlayState 感知；GAP 验收项） | play 切游戏相机+输入 | L | **GAP-2026-05-27 一部分** | **Y** | N |
| 加载/运行游戏玩法代码（脚本/dll） | 缺失（`GAP-2026-05-27` 仅登记，用户不排期 `:2176`） | 脚本运行时 / dll 热加载 | L | **大型架构，跨仓多 session** | **Y** | N |
| 代码 hot-reload / Live Coding | 缺失（v1.x 长尾未开工 `:2175`） | live coding | L | 同上 | **Y** | N |
| workspace / 外部项目模型 | 缺失（编辑器焊死自己仓 assets/ `:2196`） | 打开外部项目 | L | **PIE 硬前置** | N | Y（项目路径解析可单测） |
| 运行中实时改参 / 多窗口模拟 | 缺失（Play 期 Inspector 只读，无多 client） | 实时调参 / split-screen | L | 需 play 期可控写回 | **Y** | N |

### 2.6 动画 + VFX 工具

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| 动画状态机图编辑器 | 已实现（`AnimFsmAssetInspectorPlugin.cpp` 961 行，节点/边/命令/Save） | node-based controller | — | 无文件创建入口；边为直线占位；命令栈全局 Clear | **Y** | Y（FSM 文件 IO/命令可单测） |
| Condition/Parameter DSL 编辑 | 已实现（`AnimFsmAssetInspectorPlugin.cpp:657-842`） | 过渡条件编辑 | — | merge 未实现 | **Y** | Y（DSL parse 可单测） |
| FSM 活体预览（挂 live entity 跑） | 缺失（编辑器内无运行时翻译，`AnimFsmModel.h:11-13`） | 预览播放 | L | 需运行时翻译中间层 | **Y** | N |
| 时间轴 / dopesheet / 关键帧 | 缺失（grep 0；引擎动画模型无 keyframe/track 概念） | dopesheet + 曲线 | L | **引擎动画模型需扩 keyframe（跨仓）** | **Y** | Y（插值求值可单测） |
| 专用曲线编辑器 | 缺失（`ParticleEmitterComponent.h:16` 明示留待） | ImGui curve editor | M-L | 切线/手柄手感 | **Y** | Y（曲线求值可单测） |
| 混合树 / blend space | 缺失（引擎 FSM 为 flat-weighted） | 1D/2D blend tree | L | **引擎动画模型扩展（跨仓）** | **Y** | Y（混合权重可单测） |
| 动画事件 / notify | 缺失 | 时间轴 notify | M-L | 依赖时间轴 | **Y** | Y（事件触发可单测） |
| DragonBones 骨骼浏览 | 部分=仅 metadata（`DragonBonesAssetInspectorPlugin.cpp:113-165`） | 骨骼树浏览 | — | Preview 仅 toast | **Y** | N |
| 单 clip / 骨骼实时预览 | 缺失（`:144-160` toast，需独立 preview viewport） | clip 预览窗 | L | **需 offscreen 预览 RT（跨仓）** | **Y** | N |
| Animator backend 切换 | 已实现（`AnimatorMiniPreviewPlugin.cpp:49-108`） | 同 | — | — | **Y** | Y |
| Procedural channel 配置 | 部分=仅 name 浏览（`:140-163`） | channel fn 编辑 | M | 需 DSL/UBO 通路 | **Y** | Y |
| 粒子 Inspector 字段编辑 | 已实现（`RegisterBuiltinSchemas.cpp:229-332`） | 发射器参数面板 | — | color/size 仅 start→end | **Y** | N |
| 粒子 viewport gizmo | 部分=仅可视化不可拖（`ParticleEmitterGizmoPlugin.cpp:67-148`） | 可拖 spawn/velocity handle | M | 同 gizmo HitTest 缺口 | **Y** | N |
| 粒子 color/size 曲线 | 缺失（线性 lerp，`ParticleEmitterComponent.h:15-18`） | 曲线 modifier | M-L | 依赖曲线编辑器 | **Y** | Y |
| 粒子预览专窗 / emitter 模板库 / 节点式 VFX 图 / flipbook / GPU 粒子 | 缺失（VfxSystem 仅单层 desc） | Niagara/VFX Graph 风格 | L | **多数需引擎 VFX 能力（跨仓）** | **Y** | N |
| 底部 Animation tab（dopesheet 常规归属位） | 缺失=占位（`EditorRenderLayer.cpp:1921-1929` 两行 TextDisabled） | 填充 timeline | L | 依赖时间轴 | **Y** | N |

### 2.7 场景序列化 + Layers

> 注：场景层级（Hierarchy / EntityTree）已在独立报告覆盖，此处只列序列化 / Layers / 多场景 / prefab。

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| 单文件 Save/Load + 持久 ID 解耦 | 已实现（`SceneSerialization.cpp:245/276`，index 升序 ID） | 同 | — | 未知 component 静默 skip（文档说 warn） | N | Y |
| per-layer split + manifest | 已实现（`SceneSerialization.cpp:593/677`） | 多文件场景 | — | **跨 layer hierarchy 引用丢失**（`:113-117`，attach-time 拒绝未确认落地） | N | Y |
| schema 版本冻结 + CanRead | 已实现（`scene/world` v1.10，`SchemaVersion.h:60-62`） | 同 | — | 无 major migrator 实例 | N | Y |
| Layers CRUD + 可见性 | 已实现（`WorldPartition.cpp` / `LayersPanel.cpp`） | 同 | — | — | **Y**（toggle 手感） | Y（partition 逻辑可单测） |
| Layer 重命名 / 排序 / entity count / DnD 改归属 | 缺失（`LayersPanel.cpp:13-18` 明列不在范围） | 同 | S-M | DnD 焦点竞争 | **Y** | Y（rename/reorder 数据可单测） |
| 多场景同时编辑 / active scene | 缺失（`EditorSceneContext.h` 单 `pWorld`） | 多场景 + active scene | L | 整体架构改动 | **Y** | Y（多 world 容器可单测） |
| 子场景 / scene 引用 / prefab | 缺失（grep 0；per-layer split 非 scene 引用） | prefab + 嵌套 + override | L | **需 EntityGUID 稳定引用，跨仓前置** | **Y** | Y（prefab 实例化/override diff 可单测） |
| Autosave（编辑器接线） | 部分=引擎 `AutosaveScheduler` 存在但编辑器零接线（`tools/OrangeEditor/` grep 0） | 编辑器自动存档 | S-M | 落点/节流 | **Y** | Y（scheduler 已有 test `AutosaveSchedulerTest.cpp`） |
| Dirty 跟踪 + 未保存确认 | 已实现（`EditorSceneContext.h:116`；modal `EditorRenderLayer.cpp:543-579`） | 同 | — | **材质改动不置 dirty**（旁路陷阱） | **Y** | Y（dirty 钩子可单测） |
| ComponentSerializers（16 内置 + extraSerializers 扩展点） | 已实现（`ComponentSerializers.cpp:1715-1738`） | 同 | — | — | N | Y |

### 2.8 编辑器基建（docking / 命令栈 / console / settings / keybindings）

| 能力 | 当前状态 | 成熟对应 | 量 | 风险 | GUI dogfood? | 纯逻辑单测? |
|---|---|---|---|---|---|---|
| ImGui Docking + 多视口 | 已实现（`main.cpp:346-347`；`UpdatePlatformWindows` `EditorRenderLayer.cpp:391`） | 同 | — | 子窗口 icon 不继承；DPI 全局兜底 | **Y** | N |
| 编程式默认布局 + maximize 重建 | 已实现（`BuildDefaultLayoutOnce` `:432-474`，对称阈值重建 `:310-324`） | 同 | — | maximize-on-startup 是 workaround 非 root-cause | **Y** | N |
| Dock 布局持久化（imgui.ini） | 已实现（隐式，依赖 cwd chdir） | 同 | — | ini 路径未显式管控；无"重置布局"菜单（未确认） | **Y** | N |
| 命令栈（coalesce / group / 容量 200 / World 解耦） | 已实现（`CommandStack.cpp`；`SetFieldValueCommand.h:45-53`） | UndoRedo 系统 | — | 嵌套 BeginGroup 不支持 | **Y**（合并手感） | Y（coalesce/group/淘汰可单测） |
| 字段编辑/创建/reparent/重命名/layer/backend undo | 已实现（多命令类型） | 同 | — | — | **Y** | Y |
| 破坏性操作 undo（删实体/组件增删/删 layer） | 缺失=均走 `Clear()`（`EntityTreePanel.cpp:191` / `InspectorPanel.cpp:160` / `SchemaInspector.cpp:1032` / `LayersPanel.cpp:210`） | 可 undo 的破坏性操作 | L | 解锁需完整序列化（`CommandStack.h:11-25` 标 v0.3+ 未发生） | **Y** | Y（删除序列化往返可单测） |
| Undo/Redo 菜单显示具体动作名 | 缺失=固定文案"Undo"/"Redo"（`EditorRenderLayer.cpp:664,670`，无命令历史面板） | "Undo Move Entity" + 历史面板 | S-M | 低 | **Y** | Y（label 生成可单测） |
| Console（sink/ring buffer/level 过滤/搜索/着色） | 已实现（`EditorRenderLayer.cpp:1941-2035`） | 同 | — | 无时间戳/来源列；搜索大小写敏感；无双击跳源 | **Y** | Y（过滤/level 逻辑可单测） |
| Console 双击跳转源 | 缺失 | 双击跳源码 | M | 需源定位 | **Y** | N |
| EditorSettings 持久化 | 已实现但仅 gizmo 视觉常量（`EditorSettings.h:36-61`） | 通用偏好（相机速度/grid/单位/语言） | M | 内容窄 | **Y** | Y（JSON 往返可单测） |
| Keybindings rebind | 已实现 6 条单键（`EditorKeybindings.h:22-36`，真接线） | 全量可 rebind + 冲突检测 | M | chord（Ctrl+Z/S/N...）仍 hardcode；无冲突检测 | **Y** | Y（read/clamp/冲突检测可单测） |
| 退出 / 未保存拦截（×/Esc/New/Open） | 已实现（`main.cpp:774-786`；modal `:543-579`） | 同 | — | **仅拦截 scene，材质改动不拦截** | **Y** | Y（pendingCloseAction 状态机可单测） |
| Profiler（CPU/GPU/内存 timeline） | 未评估（View 菜单有 Profiler 项 `:680-686`，本次审计未盘其能力深度） | in-engine flame/timeline | — | 未评估 | **Y** | 未评估 |
| 帧调试器（frame debugger） | 未评估（现状审计未覆盖） | 逐 draw call 回放 | L | **需引擎渲染侧（跨仓）** | **Y** | N |

---

## 3. 跨子系统优先级

### P0 — 立即（低风险、补已暴露的真陷阱 / 解锁日常手感）

| 项 | 子系统 | 理由 |
|---|---|---|
| **材质改动接入 dirty + 命令栈** | 材质 / 基建 | 当前改材质不置 `scene.dirty`、关编辑器不拦截（`MaterialAssetInspectorPlugin.cpp:454`）——**会真丢用户工作**，是已暴露的数据丢失陷阱，最高优先。 |
| **Gizmo snap（grid/angle/step）** | Gizmo | 关卡拼装的核心日常手感缺口，量化内核可单测、UI 体量小（对标 settings 字段扩展）。 |
| **Gizmo Local/World 切换** | Gizmo | 通用基线，渲染出身作者摆物体的高频痛点；轴向量计算可单测。 |
| **视口 Ctrl/Shift 多选 + 相机 pan** | Gizmo / 视口 | 与已覆盖的 Hierarchy 多选打通；pan 是导航刚需。注意本项目踩过 Ctrl-toggle bug，必 dogfood。 |
| **编辑器 autosave 接线** | 场景 | 引擎 `AutosaveScheduler` + test 已就绪（`AutosaveSchedulerTest.cpp`），编辑器只差接线——崩溃防丢，性价比极高。 |

### P1 — 近期（单仓可推进，价值高但体量 M-L）

| 项 | 子系统 | 理由 |
|---|---|---|
| 资产浏览器搜索 + 类型过滤 | 资产 | 资产变多后查找成本陡增，过滤谓词可单测。 |
| Inspector multi-edit 共有属性写回 | Inspector | banner 已占位，价值明确；共有属性求交 + 批写可单测。 |
| 破坏性操作 undo（删实体/组件增删/删 layer） | 基建 | 当前全走 `Clear()`，误删无救；解锁需完整序列化往返，体量 L 但价值高。 |
| Layer 重命名/排序/DnD 改归属 | 场景 | LayersPanel 明列未做项，与多选打通。 |
| 多选群组变换（pivot） | Gizmo | 数学可单测但手感必 dogfood，体量 L。 |
| 资产删除/重命名 + 依赖追踪 | 资产 | 删改前看牵连，避免断引用；需先建依赖图基础设施。 |
| EntityRef DnD 写入 + foldout 子段 + HDR 颜色 | Inspector | 一组中等改进。**EntityRef 稳定引用 + prefab 共享 EntityGUID 前置（见下）。** |

### P2 — 长尾（大型架构 / 需引擎或渲染器侧先提供能力 → 跨仓多 session）

| 项 | 前置依赖（跨仓） | 理由 |
|---|---|---|
| **Wireframe / Unlit / Normals / Overdraw / G-buffer debug view** | **OrangeRender**：wireframe pass / 通道可视化 / RT 暴露 | 渲染出身作者最熟最想要，但 render 公共头 0 命中——必须先在 OrangeRender `incoming_feature.md` 登记 → session A 实现 → bump pointer → session B 编辑器消费。**已部分登记**（`ScenePanel.cpp:161`）。 |
| **缩略图 / 材质预览球 / 单 clip 预览 / RT 缩略图** | **OrangeRender**：offscreen RT API | 全部卡在 offscreen 渲染 + mini-pipeline（`editor-roadmap.md:902`）。同跨仓节奏。 |
| **Shader 热重载 / runtime GLSL 编译** | **OrangeRender/引擎**：vendor glslang + Material UBO | `engine-known-gaps.md:1313-1321` 明示硬前置。 |
| **时间轴 / dopesheet / 曲线 / blend tree / 动画事件** | **引擎动画模型**：扩 keyframe/track 概念 | 引擎动画模型当前无 keyframe（`include/orange/engine/animation`），是跨仓引擎侧能力，非编辑器单仓可做。 |
| **Prefab（创建/实例化/override/嵌套/variant）** | **引擎**：稳定 EntityGUID 引用序列化 | 跨 layer hierarchy 已暴露引用丢失（`SceneSerialization.cpp:113`），prefab 需更强的 GUID 引用——引擎序列化侧前置。 |
| **Play-in-editor（玩法代码/脚本/dll/workspace/相机输入切换）** | **引擎 + 编辑器架构**：workspace 项目模型 + ISystem 发现 | `GAP-2026-05-27`，用户明确不排期（`:2176`）。最大型，多 session。 |
| **FBX/EXR/KTX importer + texture cook + file watcher** | vendor importer / 性能 milestone | 按需 pull-driven。 |
| **节点式材质图 / 节点式 VFX 图 / GPU 粒子 / 地形 / 脚本** | 引擎 + 渲染器大量前置 | `engine-known-gaps.md` 标 Phase 11+ 或永不；地形/脚本现状审计未覆盖（**未评估**），按成熟编辑器是缺失项但远期。 |

**跨仓纪律提醒**：上表所有标"前置依赖（跨仓）"的项，按 ADR-009 必须走"OrangeRender/引擎子仓 session 实现 → umbrella bump pointer → 编辑器子仓 session 消费"三段式，**不可同 session 双向操作**。登记入口：渲染能力 → `OrangeRender/docs/incoming_feature.md`；引擎能力 → `OrangeEngine/docs/engine-known-gaps.md`。

---

## 4. Quick Wins（1-commit 级、低风险、对标 warning chip / sibling reorder）

按性价比排序，每条都可单 commit 收口：

1. **材质改动置 `scene.dirty`**（材质）——在 `MaterialAssetInspectorPlugin` Save/改值处接 `scene.dirty=true`，堵住"改材质关编辑器不提示"的真陷阱。最优先，几行改动。
2. **"Save As New Material" 入口**（材质）——`engine-known-gaps.md:621-624` 已登记为可选，复用 Create Material modal，S 级。
3. **View toggle（Grid/Sky/DebugDraw/Colliders）持久化**（视口）——把 file-static（`ScenePanel.cpp:47`）迁进 EditorSettings，重启保留。
4. **相机 saved view 字段 + 持久化**（视口）——`EditorCameraState` 加字段 + 序列化往返，可单测。
5. **HDR 颜色 flag**（Inspector）——`SchemaInspector.cpp:286` 给 ColorEdit 传 HDR flag + isHdr attribute。
6. **游戏侧 `RegisterComponentSchema<T>` 封装 API**（Inspector）——补上文档承诺但不存在的封装（`ComponentSchemaRegistry.h`），消除 doc/code 不符。
7. **Undo/Redo 菜单显示具体动作名**（基建）——把 `GetType()` 映射成用户可读 label（`EditorRenderLayer.cpp:664`）。
8. **Console 时间戳列 + 大小写不敏感搜索**（基建）——`DrawConsolePanel` 小幅扩展。
9. **`scene/world` 未知 component 真发 warning**（场景）——修文档/实现不符（`SceneSerialization.cpp:399`），或退一步把头文件 contract 改对齐"silent skip"。
10. **snapshot 文件名加唯一后缀**（PIE）——`EditorRenderLayer.cpp:1033` 固定名改唯一名，对齐 `EditorSceneContext.h:124` 注释承诺，消除多实例隐患。

> 注：1/2/3/5/7/8/10 涉及 ImGui 交互或视觉，仍建议作者顺手 dogfood 一眼；4/6/9 核心是纯逻辑可先单测。

---

## 5. Workflow 加速 vs 人工 GUI dogfood 分工

**分工原则一句话**：**凡"给定输入算出确定输出"的逻辑内核（数学 / 序列化往返 / 状态机迁移 / 过滤谓词 / 命令 coalesce）一律 workflow 自动化 + 单测先行兜底；凡"鼠标落点 / 拖拽手感 / 焦点竞争 / 视觉对齐 / 帧间反馈"一律作者真人 GUI dogfood，单测无法替代——本项目连续 3 个交互 bug（DnD / 双击 / Ctrl-toggle）全部逃过逻辑测试、只在真人点拖时暴露，是这条原则的硬证据。**

**可 workflow + 单测覆盖逻辑内核的 gap**（落地前先写测试，回归免人工）：
- Gizmo：snap 量化函数、local/world 轴向量、多选 pivot 与保相对偏移的变换数学、ray-AABB/ray-tri 求交。
- 序列化：单文件/per-layer/prefab 实例化/override diff/快照还原的 **save→load 往返等价性**；schema 版本 CanRead 边界；持久 ID 升序归一化。
- 命令栈：coalesce 键匹配、group 入栈计数、容量淘汰、删除/组件增删的序列化往返（解锁 undo 的前置正确性）。
- 资产：hash 短路、过滤谓词、依赖图反查、文件夹递归枚举、引用重写。
- 动画/粒子：FSM 文件 IO + 命令、condition DSL parse、曲线/插值求值、混合权重。
- 状态机：Play 三态迁移守卫、pendingCloseAction、dirty 钩子。
- Settings/Keybindings：JSON 往返、clamp、冲突检测。

**必须作者人工 GUI dogfood 的 gap**（逻辑测过仍可能错，手感/反馈是验收本体）：
- 所有 gizmo 拖拽（snap 落点跟手感、平面/屏幕 handle 与单轴的 hit-test 分流、plugin gizmo handle 接管 LMB）。
- 视口框选 / Ctrl-Shift 多选 / 相机 pan-orbit-fly 的修饰键与 gate 竞争。
- 一切 DnD（asset→字段 / asset→viewport / entity→layer / dock 拖出）——本项目 DnD 踩过 bug。
- 双击进入编辑（rename / asset 打开）——踩过双击 bug。
- Inspector multi-edit 的"显示 primary 但写全选"是否符合心智、foldout 展开收起、HDR 取色器观感。
- 缩略图/预览球/debug view 的**视觉正确性**（颜色空间、y-flip、过暗——本项目 GTAO/tonemap 都因视觉问题被 dogfood 逮到）。
- dock 布局 maximize/restore 重建、多视口窗口拖出、未保存 modal 的焦点与按钮路径。

**最高杠杆做法**：对每个 P0/P1 gap，先用 workflow 把逻辑内核写成单测锁住正确性（防回归 + 提速迭代），再让作者在真实编辑器里做一次聚焦 dogfood 专攻"逻辑测不到的交互层"——把人的注意力集中在机器测不了的地方。

---

**文档自洽性说明**：本报告未覆盖能力深度评估的项（Profiler 能力深度、frame debugger、terrain、文本/可视化脚本）均标"未评估"，因八子系统现状审计未盘查；地形 / 脚本按成熟编辑器为缺失项但属远期 P2，需引擎侧大量前置。所有"已实现/部分/缺失"判定与 file:line 证据均取自现状审计原文，未额外臆造。