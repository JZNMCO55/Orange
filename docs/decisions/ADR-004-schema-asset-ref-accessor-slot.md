---
id: ADR-004
title: OrangeEditor v0.9.5 Schema AssetRef accessor 选型 —— 专用槽位（方案 B）而非扩 GetFn/SetFn 全字段加 ctx（方案 A）
status: accepted
date: 2026-05-19
deciders: [solo-dev]
related:
  - docs/editor-roadmap.md
  - docs/decisions/ADR-001-editor-schema-first-and-no-hardcode.md
  - vendor/Orange-Wiki/wiki/concepts/editor/property-reflection.md
  - vendor/LumixEngine/src/engine/reflection.h
  - tools/OrangeEditor/schema/PropertyDescriptor.h
  - tools/OrangeEditor/schema/SchemaInspector.cpp
  - tools/OrangeEditor/schema/RegisterBuiltinSchemas.cpp
---

## Context

v0.8 c5 自承诺 "完整 `(Component&, EditorAssetContext&)` 签名整骨留 v0.9"，v0.9 完工 retro 评估推延到 v0.9.5 patch milestone（与 v0.6.5 / v0.8.5 同节奏）。

### 当前状态（2026-05-19）

- `RegisterBuiltinSchemas.cpp` 内有 file-scope 静态指针 `gpAssetContext`（anonymous namespace 内），main.cpp 启动期通过 `SetEditorAssetContextForSchema(&editorHost.assets)` 注入
- AssetRef get/set lambda（6 个：Renderable mesh + materialInstance + Environment cubemap 的 get/set 各两个）通过 `gpAssetContext->pAssets` + `gpAssetContext->namedMaterialInstances` 完成 `std::string ↔ AssetHandle / MaterialInstance*` 双向转换
- 痛点：file-scope 单例违反 CLAUDE.md "OrangeEditor 架构纪律" 节关于"无脑加字段 / 隐式全局状态"的精神；与 EditorState god struct 拆分（v0.2.5 整骨）逻辑相同——schema 注册层不该有"启动期注入的隐式 ctx"

### 关键约束（决定选型）

1. **capture-less lambda 必须**：PropertyDescriptor get/set 是 `void (*)(...)` 函数指针，不允许 `std::function` —— 性能 / inline 友好 / 零堆分配的硬约束（CLAUDE.md "no premature abstraction"），lambda 不能捕获局部 ctx
2. **OrangeEngine 组件存 AssetHandle**（不是 Lumix 那样存 `Path`），所以 schema 层做 path↔handle 转换确实需要 AssetRegistry —— 这是真实的架构错配，不是设计失误
3. **Lumix reflection.h:130** 的 setter 签名是 `void (*)(IModule*, EntityRef, u32, const T&)` —— **不带 editor context**，因为 Lumix 用 ResourceManager 作 engine-level service 完成 path 加载；OrangeEngine 没有 engine 内置的 Path 解析路径，schema 层只能自己做

### 90/10 用例分布

- AssetRef 字段：6 个 lambda（仅 Renderable / Environment 两个 component）
- 非 AssetRef 字段：~30 个 builder 站点 + ~10 个 FieldCustom（ParticleEmitter / Collider variant 字段）
- 也就是说"需要 ctx"的场景占比 < 20%，其余 80%+ 字段是纯数据 get/set

## Options Considered

### 方案 A：扩 `PropertyDescriptor::GetFn / SetFn` 签名加 `const EditorAssetContext& ctx`

`GetFn = void (*)(const void* component, const EditorAssetContext& ctx, void* outValue);`（SetFn 同款）

- **优点**：
  - 未来扩展性强：若再有 ScriptRef / LocaleRef / 动态枚举源等"ctx-hungry"字段类型，零额外架构改动
  - 统一心智模型：所有 property 都拿 ctx，调用站点写法对称
  - 与"显式依赖"架构原则更纯粹一致
- **缺点**：
  - 改动面 ~40 个 SchemaInspector dispatch 站点（每个 PropertyType case 的 get/set 调用）+ 命令栈 ApplyFn lambda 全数加 ctx 透传 + 所有 FieldCustom 注册 lambda 改签名（ParticleEmitter 8 个 / Collider 6 个 / Animator 1 个）
  - 90%+ 字段实际不需要 ctx，每个 lambda 签名多一个无用参数 —— 类型擦除前提下编译器 inline 后开销可忽略，但**视觉污染**显著
  - 与 Lumix 模式偏离（Lumix setter 不带 ctx）—— 同栈参考引擎不这么做

### 方案 B：`PropertyDescriptor` 加 `AssetRefGetFn / AssetRefSetFn` 专用槽位

只 AssetRef 字段走带 ctx 的新签名 accessor；其它 PropertyType 字段 get/set 槽位不动。

```cpp
using AssetRefGetFn = void (*)(const void* component,
                               const EditorAssetContext& ctx,
                               void* outValue);
using AssetRefSetFn = void (*)(void* component,
                               const EditorAssetContext& ctx,
                               const void* inValue);
AssetRefGetFn assetRefGet = nullptr;
AssetRefSetFn assetRefSet = nullptr;
```

SchemaInspector AssetRef case：assetRefGet/Set 非空时优先走新路径；否则回退到旧 get/set（兼容外部 plugin）。

- **优点**：
  - 改动面小（PropertyDescriptor + ComponentSchemaBuilder::FieldAssetRef + SchemaInspector AssetRef case + 6 个内置 lambda + 命令栈 AssetRef 分支 ~5 处）
  - 与 Lumix 模式一致：主路径 setter 纯数据，AssetRef 是"OrangeEngine 因组件存 handle 导致的局部需求"的局部解
  - 不污染 80%+ 非 AssetRef 字段
  - 现有 19 个 builder 站点 + 15 个 FieldCustom lambda 零改动
- **缺点**：
  - PropertyDescriptor 多两个槽位（8 字节 / 32-bit fn ptr × 2 = ~16 B，按 cache line 角度可忽略）
  - 若未来引入第二类 ctx-hungry 字段，要么复用 AssetRefAccessor（语义不对），要么再加专用槽位（机制重复），要么升级到方案 A —— **本 ADR 的升级 trigger** 段就是为此

## Decision

**采用方案 B**。

### 理由

1. **don't design for hypothetical future**：当前只有 AssetRef 一类字段需要 ctx，未来是否还有第二类是不确定的；方案 A 为"假设可能出现的扩展"提前付 40+ 站点改动成本，违反 CLAUDE.md "Don't add features, refactor, or introduce abstractions beyond what the task requires" 原则
2. **与 Lumix 模式一致**：同栈参考引擎的 setter 不带 editor context，AssetRef 是真实架构错配的局部解，专用槽位是诚实的表达
3. **未来升级到方案 A 仍然可行**：方案 B 不阻塞方案 A —— 一旦真出现第二类 ctx-hungry 字段，升级到方案 A 是机械改造（路径在本 ADR Notes 段记录）

### 升级到方案 A 的 trigger 条件

满足以下任一条件，应另开 session 评估升级到方案 A：

- 出现第二类 ctx-hungry 字段类型（如 ScriptRef / LocaleRef / dynamic-enum-source / 任何依赖 `EditorAssetContext` 之外其他 sub-context 的字段）
- 同一字段同时需要多种 ctx（如 AssetRef × LocaleRef 组合）
- 外部 plugin 反复抱怨"为我的字段类型也加专用 accessor 槽位"

升级路径：

1. 把 `GetFn / SetFn` 签名扩为 `(const void* / void*, const EditorAssetContext& ctx, ...)`
2. SchemaInspector 所有 dispatch 站点改透传 ctx；MakeFieldApply 模板捕获 EditorHost*
3. `AssetRefGetFn / AssetRefSetFn` 槽位降级为 `[[deprecated]]` 别名一期，下一 milestone 移除

## Consequences

### 正面

- `RegisterBuiltinSchemas.cpp` 内零 file-scope 单例 / 零启动期注入 —— `gpAssetContext` + `SetEditorAssetContextForSchema` 双双下架
- main.cpp 启动序列减少一行隐式依赖注入
- AssetRef Undo/Redo replay 路径走显式 ctx —— 即使未来 EditorHost 重构 / EditorAssetContext 拆分（e.g. AssetRegistry 独立），命令栈不会因为 file-scope 指针 dangling 崩
- SchemaInspector AssetRef case 双路径 dispatch（c2 新增的 `useCtxAccessor` 分支）为外部 plugin 提供向后兼容缓冲——内置 schema 全走新路径，外部仍可用旧 GetFn/SetFn 完成注册（c2 保留的 fallback 分支）

### 负面 / 待还的债

- PropertyDescriptor 内存布局多 16 字节（两个函数指针）—— 32 字节缓存行内仍 fit，可忽略
- 双路径 dispatch（c2 的 `useCtxAccessor` 分支选择）增加 SchemaInspector AssetRef case ~30 行胶水代码 —— 一次性成本，c3 之后内置 schema 不再触发回退分支
- 文档 / 注释里需要持续提醒 "AssetRef 走 assetRefGet/Set，其它走 get/set" 的差异 —— 通过 PropertyDescriptor.h 类型定义处的 30 行注释 + FieldAssetRef builder 注释承担

### 强制 invariant（新沉淀）

无新增项目级 invariant。本决策是 ADR-001 "schema-first" 框架内的局部选型，不改变 OrangeEditor 架构纪律节的任何禁令；CLAUDE.md "Serialization and reflection" 节关于"反射库禁令"在本 ADR 下保持—— `AssetRefAccessor` 是手写 typedef + 函数指针，非反射库。

## Notes

- 本 ADR 是 v0.8 c5 自承诺"完整 (Component&, EditorAssetContext&) 签名整骨" 的最终交付形态。原始 v0.8 c5 注释里把这条债定性为"需要扩 PropertyDescriptor + SchemaInspector dispatch + 所有 Field<T>::register 站点"——这是方案 A 的描述；评估后采用方案 B 是因为"全字段"成本不对称（90% 字段不需要 ctx）。
- Lumix 模式参考：`vendor/LumixEngine/src/engine/reflection.h:130` setter `void (*)(IModule*, EntityRef, u32, const T&)`，配合 `vendor/LumixEngine/src/editor/property_grid.cpp` 的 GridUIVisitor 持有 `WorldEditor&`（包含 ResourceManager / CommandStack）；Lumix 没有"setter 拿 editor context"的形态，因为它的组件直接存 `Path`，由 engine 级 ResourceManager 解析
- wiki 关联：`vendor/Orange-Wiki/wiki/concepts/editor/property-reflection.md` §"陷阱"第 3 条 ("属性 Visitor 中修改属性必须走 Command，直接调用 setter 会绕过 Undo 系统") 与本 ADR 升级 trigger 的"AssetRef Undo/Redo replay 走 ctx" 部分内在一致——命令栈持有 setter 是其正确形态
- 若有兴趣补 wiki 一页 "editor-context-dependent property accessor"（讨论本议题的工业实践），另开 Orange-Wiki 维护 session 处理；不在本 milestone 范围
