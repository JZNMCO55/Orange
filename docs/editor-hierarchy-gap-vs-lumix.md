<!-- 本文档由 Claude Code workflow（orange-editor-vs-lumix-hierarchy，7 子代理，2026-05-29）自动综合生成。证据列 file:line 取自当时代码审计，随代码演进可能漂移。全编辑器其余子系统见 editor-capability-gap-vs-mature.md。 -->

# OrangeEditor 场景层级编辑 · Gap 报告与 Port 路线

> 参照系说明：本报告的"成熟编辑器对应"来自**成熟编辑器模式知识**（Lumix 架构惯例 + Unity/Unreal/Godot 通用预期），**非真 Lumix 源码逐行核对**。凡现状审计 4 份未覆盖的区域，明确标注"未评估"，不臆测。

---

## 1. 一句话总览

OrangeEditor 当前层级编辑已经走通**单场景内的核心闭环**——单选 + Ctrl 多选（数据层）、into-reparent / 子节点 before-after reorder / detach-to-root、双击+F2 内联重命名、右键 Create/Delete/Rename/Move-to-layer、layer chip + singleton-overflow warning chip，且**非破坏性操作全带精确 Undo**（reparent 精确复位、gizmo 事务分组、字段 coalesce、跨场景命令防护扎实）；离 Lumix-级成熟主要差**五大类**：（1）**Prefab / 实例 / Duplicate 整块为零**——这是最大空白；（2）**剪贴板（cut/copy/paste）整块为零**；（3）**多选只是"展示态"**——additional set 无任何批量操作消费它（批删/批移/批 frame/multi-edit 全缺）；（4）**破坏性结构操作（删实体/加删组件）不可撤销**且连带清空整个 undo 栈；（5）**大纲面板的"找得到/看得清"层**几乎空白——无搜索过滤、无可见性/锁定 toggle、无类型图标、无 Shift 范围选、无根级 reorder。

---

## 2. Gap 表

> 「需 GUI dogfood 验证?」判定原则：凡涉及 ImGui 交互/拖拽手感/视觉反馈/键盘焦点竞争 → **Y**（本项目刚连续踩 DnD 锚点、双击重命名两个只有真人点/拖才暴露的交互层 bug，从严）。纯数据结构/序列化/命令反向逻辑可脱 GUI 单测 → 该列 Y。

| 能力 | 当前状态（证据） | Lumix/成熟对应 | 工作量 | 主要风险 | 需 GUI dogfood? | 可纯逻辑单测? |
|---|---|---|---|---|---|---|
| Duplicate 实体/子树（Ctrl+D） | **缺失**（全仓 grep 场景层零命中；EntityTreePanel/EditorHierarchy 无 clone 命令） | 通用：copy+paste 复合，走序列化 + 子树内部引用 remap | L | 子树内部 EntityRef 互引 remap 错→引用错位/悬挂；新实体持久 ID 分配 | Y（落点/选中/重命名手感） | Y（remap 映射、子树深拷贝正确性可纯逻辑测） |
| Copy / Paste / Cut 实体 | **缺失**（EditorSelection 无 clipboard 字段；无剪贴板数据结构） | 通用：序列化进内存 blob，paste as child/sibling，跨场景搬运 | L | 同 Duplicate + 跨场景粘贴时 layer/持久 ID 重映射；paste 落点语义 | Y（粘贴落点、跨面板交互） | Y（序列化 round-trip + remap） |
| Prefab 资产（create / instantiate） | **缺失**（全仓 grep prefab 场景层零命中；引擎 HierarchyComponent 是裸数据，无 prefab loader/实例化路径） | Lumix：`.fab` + 稳定 EntityGUID + PrefabSystem 双向 map | L（需先小 ADR + 引擎层 EntityGUID 能力） | 跨 session 跨仓纪律（引擎能力 vs 编辑器消费需分仓落地）；EntityGUID 是引擎层新概念 | Y（拖入实例化、override 标记交互） | Y（序列化/GUID 分配/实例 map 纯逻辑） |
| Prefab 实例 override / revert / apply | **缺失** | 通用强项（Unity 蓝条）；Lumix override 颗粒度本就弱 | L | 颗粒度模型选型（逐属性 vs 整体解耦）；依赖 prefab 先落地 | Y（蓝条标记、revert 右键） | 部分（override diff 计算可逻辑测，标记渲染需 GUI） |
| 嵌套 prefab / 继承场景 | **缺失** | Godot 强项（场景即 prefab）；Lumix prefab 套 prefab | L | 依赖 prefab 基建；递归实例化复杂度 | Y | 部分 |
| 多选**批量删除** | **缺失**（Del 只删 primary，不遍历 additional set） | 通用：命令对选中集合批量作用 | M | 批量删除应打成**一个 undo 组**（但删除当前不可撤销→设计冲突，需先解删除 undo） | Y（多选后 Del 的确认手感） | Y（遍历集合 + 组打包逻辑） |
| 多选**批量 reparent / move-to-layer** | **缺失**（payload 单 entity；pendingReparent/pendingDelete 均单值） | 通用：批量作用于 m_selected_entities，打包单 undo | M | 批量 reparent 防环需对每个源逐一校验；原子分组 | Y（多选拖拽手感） | Y（批量防环 + 分组逻辑） |
| 多选**批量 transform**（gizmo 拖整组） | **缺失**（gizmo 只读 selectedEntity，ScenePanel 调度只传 primary） | 通用：gizmo 落在选区 pivot，delta 应用到每个成员 | M | pivot 选取（首选实体 vs 包围盒中心）；多实体 delta 分发 + 事务分组 | Y（gizmo 多选手感是重灾区） | 部分（delta 数学可测，pivot/拖拽需 GUI） |
| 多选 Inspector multi-edit（共有属性） | **缺失（仅 banner 提示）**（InspectorPanel.cpp:70-82 "Multi-edit not yet wired"，注释留 v0.8/v0.9） | 通用：显示共有组件、改值广播全选区 | L | schema-first 异构多选→共有属性求交集；混合值 UI 表示 | Y（多选编辑广播视觉） | Y（共有属性求交 + 广播逻辑） |
| 多选 **Frame**（包整组 AABB） | **缺失/部分**（FrameSelectedCamera 只读 selectedEntity 单个） | 通用：F 键包选区整体 AABB | S | AABB 合并对无 mesh 实体的退化 | Y（F 键镜头落位手感） | Y（AABB 合并纯数学） |
| viewport 内多选（Ctrl pick 进 additional set） | **缺失**（ScenePanel pick 恒覆盖 selectedEntity，从不碰 additional；多选仅 Tree 可建） | 通用：viewport 框选 / Ctrl-pick | M | pick 路径加 Ctrl 分支 + 与 Tree 路径选择模型一致性 | Y（点选手势） | 部分（选择集合状态机可测，pick 命中需 GUI） |
| 删除可撤销（恢复子树 + 引用） | **缺失（刻意）**（DestroySubtree 后 cmdStack.Clear()，连带清空全部历史；EntityCommands.h:22-25 "v0.3+ 序列化后解锁"未兑现） | 通用：delete 可 Undo 完整恢复子树 | L | 需子树序列化快照能力（与 prefab/copy 同一基建）；恢复时持久 ID/引用回填 | Y（删除→Undo 视觉一致性） | Y（快照 round-trip + 反向命令逻辑） |
| AddComponent / RemoveComponent 可撤销 | **缺失**（InspectorPanel.cpp:160-161 / SchemaInspector.cpp:1032-1046 均 add/remove 后 Clear） | 通用：组件增删走可逆命令 | M | 组件状态快照（依赖序列化）；RemoveComponent 需存被删组件全量数据 | Y（Inspector 增删 + Undo） | Y（组件快照 + 反向命令） |
| 名称搜索 / 过滤框 | **缺失**（全文无 ImGuiTextFilter；root 枚举全量画树无过滤分支） | 通用：ImGuiTextFilter 子串匹配，保留命中节点祖先链 | S | 过滤时维持树结构（命中节点的祖先链需可见） | Y（输入即时筛选手感） | 部分（匹配 + 祖先链算法可测，渲染需 GUI） |
| 类型过滤（by component） | **缺失** | 通用：只看 Light/Camera/Mesh 某类 | S | 遍历各 component view 反查；与名称过滤组合 | Y | Y（过滤谓词逻辑） |
| 可见性 toggle（眼睛图标，editor-only） | **缺失**（RenderableComponent.visible 字段存在但 Tree 行无开关）；Lumix 本身此项偏弱 | 通用（Unity/UE 强）：per-entity 编辑器可见 flag，沿层级继承 | M | "编辑器可见" vs "运行时 enabled" 语义需分清（不能混用 RenderableComponent.visible）；继承传播 | Y（图标点击 + 显隐反馈） | 部分（继承传播逻辑可测，渲染抑制需 GUI） |
| 锁定 toggle（锁图标，禁选/禁拖） | **缺失** | 通用（Unity/UE 强）；Lumix 偏弱 | M | lock 需拦截 pick + DnD source + Del 多条交互路径（易漏副路径，参照本项目 push-constant 副路径审计教训） | Y（锁后所有交互路径都要点验） | 部分（lock 状态查询可测，拦截覆盖需 GUI 全路径走查） |
| 节点类型图标（per-type icon） | **缺失**（label 只用名字字符串，无 icon 前缀） | 通用：每类对象独特图标 | S | 图标字体/纹理资产接入；按 component 分类映射 | Y（视觉，需真人看排版） | Y（type→icon 映射纯逻辑） |
| 节点颜色编码 | **部分**（仅 warning chip 黄色；selected 走 ImGui 默认高亮；无类型色/disabled 灰显） | 通用：item color / 功能区配色 | S | 配色与 warning 黄/selected 高亮冲突 | Y（视觉判读） | Y（color 映射） |
| Shift 范围选 | **缺失**（注释明确留 v0.8，需节点顺序扁平化映射；EditorSelection.h:36-37） | 通用：Shift 区间选 | M | 依赖 tree 节点扁平化序（与根 reorder 共享"扁平序"基建） | Y（Shift 点选区间手感） | Y（扁平化序 + 区间计算逻辑） |
| Ctrl 取消 primary 自身 | **部分**（Ctrl 点 primary 自身被 `entity != selectedEntity` 排除，落 else 反清空 additional） | 通用：Ctrl 应能 toggle 掉任意成员含 primary | S | 取消 primary 后需 promote 一个 additional 为新 primary 的语义 | Y（多选 toggle 手感） | Y（选择集合状态机） |
| 根节点之间 reorder | **缺失（已登记 GAP-2026-05-29-root-reorder-not-supported，未排期）**（根无 sibling 链，按 entity id 稳定排序；MoveBefore/After 对根退化为无序 detach） | Lumix 本身 sibling 显式排序就弱；Unity 有强 sibling index | M（需小 ADR：隐藏 scene-root / sortIndex / World 级 rootOrder 三选一） | 根序无持久化表示，需引擎层引入 rootOrder 字段→schema bump（跨仓） | Y（根拖拽排序手感） | Y（rootOrder 持久化 + 排序逻辑） |
| Undo/Redo 菜单项带操作名 label | **缺失**（GetType 仅作 coalesce 判别键；Edit 菜单固定 "Undo"/"Redo"，EditorRenderLayer.cpp:664/670） | 通用："Undo Rename" / "Undo Move Entity" | S | GetType 是机器键非人类可读 label，需建 type→显示名映射表 | Y（菜单文案需真人看） | Y（映射表 + 栈顶 type 读取） |
| reparent/reorder 拖动 coalesce | **缺失（但不必要）**（LambdaCommand.h:8-11 不支持 Merge）；DnD 是"放手提交一次"离散操作，不会栈爆 | merge 概念通用，但此处性质不需要 | — | 无需做（如实记录：现状合理，非真缺口） | N | Y（如要做） |
| 视口↔大纲双向联动（Tree 选中→viewport，viewport pick→Tree 高亮滚动） | **部分**（Tree→viewport wireframe 已有 ScenePanel.cpp:250-272；viewport pick→Tree 自动滚动/展开**未评估**，4 份审计未覆盖 Tree 是否随 viewport 选择 scroll-to/expand） | 通用：双向高亮 + 自动滚动展开祖先链 | M | **未评估**——需确认 Tree 是否对外部选择变更做 ScrollToItem + 展开祖先 | Y（双向联动手感） | 部分 |
| 跨 layer 父子关系序列化 | **缺失（设计性）**（SaveSplit 按 layer 过滤后持久 ID 各文件独立从 0 编号，跨 layer 引用不被正确序列化；注释承诺 attach-time 拒绝但**未见落地**） | 未评估对应（成熟编辑器多用全局 GUID 规避） | M | attach-time 拒绝逻辑缺失=可造出存盘丢失的非法状态 | Y（跨 layer 拖拽拒绝提示） | Y（attach-time 校验谓词） |
| 多场景同时编辑 / active scene | **未评估**（4 份审计未覆盖是否支持多场景挂载） | 通用（UE 大世界 / Unity multi-scene） | — | 未评估，不臆测 | — | — |
| Isolate / Solo / Hide unselected | **缺失**（无任何隔离工作流；与可见性 toggle 同源） | 通用聚焦工作流 | M | 依赖可见性 flag 基建 | Y | 部分 |
| 批量重命名 + 编号规则 | **缺失** | 通用（`Wall_001..NNN`、查找替换） | S | 命名冲突/去重提示 | Y（批量改名预览） | Y（命名规则生成纯逻辑） |

---

## 3. 优先级分层

### P0 立即（当前闭环里"已搭了一半、补完即解锁大量价值"的）
- **多选批量操作消费 additional set（批删 / 批 reparent / 批 move-to-layer）**：多选数据结构、视觉高亮、Inspector 计数提示都已就绪，唯独**无任何动作消费它**——这是"功能做了一半"的最高 ROI 缺口。理由：投入 M、解锁"多选"从展示态变为可用态。
- **删除 / 加删组件可撤销**（连同其依赖的子树序列化快照基建）：当前删一个实体连带**清空整个 undo 历史**（transform/rename 全没），是用户感知最痛的"反直觉"行为，且违反成熟编辑器最基本预期。理由：debt 已文档化、损害信任、且是 copy/paste/prefab 的共享基建（一次投入多处复用）。

### P1 近期（用户感知强、ROI 高、相对独立）
- **名称搜索 / 过滤框**（S）：海量节点场景定位刚需，ImGuiTextFilter 成本低。
- **可见性 / 锁定 toggle**（M）：成熟编辑器 ROI 最高前几项（参照清单末段明示）；注意 lock 需走全交互副路径，从严 dogfood。
- **多选 Frame（包整组 AABB）**（S）+ **viewport 内多选**（M）：让多选在 3D 视口里真正可用。
- **节点类型图标 + 颜色编码**（S+S）：低风险、视觉判读价值高。

### P2 长尾（大基建 / 需先 ADR / 依赖前置能力）
- **Prefab / 实例 override / 嵌套 prefab**（L×3）：最大空白但需引擎层 EntityGUID 新概念 + 小 ADR + **跨 session 跨仓**落地（引擎提供能力 → umbrella bump → 编辑器消费，三 session 分离），不可在单 session 速成。
- **Copy / Paste / Cut**（L）：与 prefab/delete-undo 共享序列化基建，建议 delete-undo 基建落地后顺势做。
- **根节点 reorder**（M，已登记 GAP）：需小 ADR 选 rootOrder 落地方案 + 引擎层 schema bump（跨仓）。
- **multi-edit 共有属性**（L）：schema-first 异构求交集复杂，注释已留 v0.8/v0.9。
- **Isolate/Solo、批量重命名、跨 layer 引用拒绝、多场景编辑（未评估）**。

---

## 4. Quick Wins（1-commit 级、低风险、可马上做）

体量对标本 session 已做的 warning chip / sibling reorder：

1. **Ctrl 取消 primary 自身**（S）：修 `entity != selectedEntity` 这条排除，让 Ctrl 能 toggle 掉 primary（取消后 promote 一个 additional 为新 primary）。纯选择集合状态机改动，可逻辑测，GUI 验一次多选手感即可。
2. **Undo/Redo 菜单项带操作名 label**（S）：建 GetType→显示名映射表，Edit 菜单读栈顶 type 拼 "Undo Rename" 等。命名键已存在，只差投射到 UI。
3. **多选 Frame 包整组 AABB**（S）：`FrameSelectedCamera` 把 additional set 一起纳入 AABB 合并。AABB 合并纯数学可单测，F 键落位 dogfood 一次。
4. **节点类型图标前缀**（S）：按 component 类型给 label 加 icon 前缀（mesh/light/camera）。type→icon 映射纯逻辑，排版需真人看一眼。
5. **名称搜索框**（S，略大于上面四项但仍 1-commit）：顶部加 ImGuiTextFilter，过滤时保留命中节点祖先链。匹配+祖先链算法可测。

> 注：删除/组件 undo、批量操作、prefab 都**不是** quick win（依赖序列化基建或跨仓，体量 M/L）。

---

## 5. Workflow 加速 vs 人工 GUI dogfood 分工

本项目刚连续踩三个交互层 bug（DnD 锚点、双击重命名、Ctrl-toggle 边界），全是"只有真人点/拖才暴露"——**手感半场无法靠 workflow 替代，必须作者 dogfood**。分工建议：

### 适合 workflow / ultracode 加速（分析 + 逻辑半场）
- **Comprehension / 现状审计扩面**：如本报告四份审计就是 workflow 产物——把"未评估"区域（viewport pick→Tree 自动滚动联动、多场景编辑、跨 layer attach-time 拒绝是否落地）补成有证据的盘点。
- **纯逻辑 port + 单测生成**：以下条目的"逻辑内核"可由 workflow 实现并配纯逻辑单测，作者只验 GUI 表层——
  - 子树深拷贝 + EntityRef remap（Duplicate/Copy 核心）→ 序列化 round-trip 测
  - 删除/组件的反向命令 + 快照 round-trip → 命令 execute/undo 对称性测
  - 批量防环（每源逐一 IsAncestorOf）+ 命令组打包 → 集合逻辑测
  - 多选共有属性求交集（multi-edit 内核）→ schema 求交测
  - Shift 范围选 / 根 reorder 的**扁平化序算法** → 排序+区间测
  - 名称/类型过滤谓词 + 祖先链保留 → 过滤算法测
  - AABB 合并、type→icon/color 映射、Undo label 映射表 → 纯函数测
- **跨仓需求登记**（ADR-010 文档豁免）：prefab/EntityGUID/根 reorder 的引擎层需求登记进 `engine-known-gaps.md` / `incoming_feature.md`。

### 必须靠作者 GUI dogfood（手感半场，workflow 测不出）
- **一切 DnD 手感**：批量 reparent 拖拽、根 reorder 拖拽、viewport Ctrl-pick 多选、drop 落点三区视觉反馈——锚点/落点 bug 只有真人拖才暴露（已踩过）。
- **键盘焦点竞争**：rename InputText 中按 Del/Esc、F2/Del 与多选/重命名态的交互（已踩过双击重命名）。
- **gizmo 多选拖动手感**：pivot 落位、delta 分发的视觉一致性——多选 gizmo 是公认重灾区。
- **lock / visibility 的全交互路径覆盖**：lock 后 pick/DnD/Del 每条路径都要真人点验是否真被拦截（参照 push-constant 副路径审计教训，机器测难穷举副路径）。
- **视觉判读类**：类型图标排版、颜色编码与 warning 黄/selected 高亮的冲突、override 蓝条、搜索即时筛选的视觉跳动、Undo 菜单文案。
- **镜头手感**：多选 Frame 的 F 键落位是否"舒服"。

**一句话分工原则**：凡能写成"给定输入→断言输出"的（remap、反向命令、防环、求交、扁平序、过滤谓词、映射表）交给 workflow + 单测；凡判定标准是"拖起来/点起来/看起来对不对"的，作者亲手 dogfood，workflow 只能帮你把它**拆到逻辑可测的边界为止**。