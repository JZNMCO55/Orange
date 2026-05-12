---
id: ADR-001
title: OrangeEditor 转向 schema-first 架构 + 全局禁止 hardcode
status: accepted
date: 2026-05-12
deciders: [solo-dev]
related:
  - docs/editor-roadmap.md
  - CLAUDE.md
  - vendor/Orange-Wiki/wiki/concepts/gameplay/game-world-editor.md
  - vendor/LumixEngine/src/engine/reflection.h
  - vendor/godot/core/object/class_db.h
---

## Context

OrangeEditor 在 2026-05-12 完成 v0.1 / v0.1.5 / v0.2 收尾后，回看现有结构在 5 个具体维度上表现为 hardcode / god class / 缺抽象：

1. `EditorRenderLayer` 是 god class —— 9 个 `DrawInspectorXxx` 成员（Transform / Hierarchy / DirectionalLight / Renderable / RigidBody / Collider / ParticleEmitter / Animator / Name）+ 5 个 panel 函数 + Play Mode 入口全塞一个类。切到多 TU 不解决类自身的巨型问题
2. `EditorState` 是 god struct，单调增长（当前 19 字段），每个 milestone 加字段没有任何子域切分
3. 没有 `IEditorInspectorPlugin` / `IEditorGizmoPlugin` 抽象——游戏侧自定义 component 要显示在 Inspector，只能改 `EditorRenderLayer` 源码新增 `DrawInspectorXxx`；正中 Orange-Wiki `game-world-editor.md §陷阱` 第 2 条 "Per-type property hardcode → schema 驱动"
4. `CommandStack` lambda 捕获 `World*` —— scene swap / 破坏性操作必须 `Clear()`；脆弱
5. 没有 `EditorHost` / `EditorApp` 一层——`main.cpp` 直接构造 `AppHost` + `EditorRenderLayer` + `EditorState`，编辑器没有"自己的应用入口"概念

调研 `vendor/LumixEngine/src/editor/*` + `vendor/godot/editor/*` 后确认：同栈（C++ / ImGui） + 同范式（in-engine WYSIWYG）的工业级编辑器 **都是 schema-first + plugin-first**：

- Lumix：`StudioApp` + `WorldEditor` + `PropertyGrid::IPlugin` + Builder API（`src/engine/reflection.h`）
- Godot：`EditorNode` + `EditorPlugin` 家族 + `EditorInspectorPlugin` + `ClassDB` + `GDCLASS` 宏

## Options Considered

### 方案 A：维持现状 + v0.3 "保留 fast path 新增 schema"

editor-roadmap v0 草案的原决策：内置组件继续 hardcode，schema 仅为游戏侧自定义 component 服务。

- 优点：v0.3 改动面小、风险低
- 缺点：**保留双路径意味着下次新增内置组件时 hardcode 路径会再次膨胀**；与 wiki "陷阱第 2 条" 直接冲突；同栈参考引擎中没有任何编辑器走这条路

### 方案 B：v0.2.5 整骨 → schema-first（无 fallback）

新增独立 milestone v0.2.5，把 9 个 `DrawInspectorXxx` 整体清除转 schema 驱动；同步拆 `EditorState` → 4 个 sub-context（`EditorSelection` / `EditorSceneContext` / `EditorAssetContext` / `EditorCameraState`）+ 引入 `EditorHost` 单例 + 定义 `IEditorInspectorPlugin` / `IEditorGizmoPlugin` 接口；CommandStack 加 `BeginGroup` / `EndGroup` + `MergeMode` 三档 + 解 `World*` 强耦合。

- 优点：与 Lumix / Godot 架构对齐；杜绝下次再叠 hardcode 路径；v0.4 Gizmo / v0.5 Asset 浏览器都受益
- 缺点：是一个独立 milestone 量，需要在 v0.3 之前插队

### 方案 C：彻底推倒 OrangeEditor 重做

放弃 v0.1 ~ v0.2 已落地代码，从空白开始按 Lumix 架构重写。

- 优点：理论最干净
- 缺点：v0.1 / v0.1.5 / v0.2 的视觉栈集成 / Demo scene / 命令栈基础**没有错**，硬抹掉是工程浪费

## Decision

**采用方案 B**。v0.2.5 整骨作为 critical-path milestone，**在 v0.3 之前完成**；同时把"OrangeEditor 禁止 hardcode"沉淀为 CLAUDE.md 项目级 invariant，与 Header isolation / Phase scope 同级严肃。

关键澄清：CLAUDE.md "Serialization and reflection" 节禁的是**反射库**（`entt::meta` / RTTR / cereal-with-reflection / clang AST codegen）。Lumix Builder API 和 Godot GDCLASS 都是**手写宏 + 模板特化**，编译期注册，零运行时反射库依赖——**不在禁令之列**。v0.2.5 走这条路。

## Consequences

### 正面

- OrangeEditor 后续 milestone（v0.4 Gizmo / v0.5 Asset 浏览器 / v0.7 Animation 子模式）落在干净抽象上，工程量更可预测
- 游戏侧自定义 component 在 v0.3 之后零编辑器源码改动即可注册显示（L2 限制消除）
- 与同栈工业级编辑器（Lumix / Godot）的代码结构对齐，未来招协作者 / 接受贡献的认知成本低

### 负面 / 待还的债

- v0.3 推迟 ~ 1 个 milestone（v0.2.5 量约 2 周——schema 基础设施 + 9 个组件转 schema + CommandStack 重构 + 回归测试）
- 整骨过程中 v0.1 ~ v0.2 已有功能（场景保存 / 加载 / Undo / Redo / Play Mode）需要全量回归——验收硬指标已写进 editor-roadmap.md v0.2.5 节

### 强制 invariant（已沉淀到 CLAUDE.md "OrangeEditor 架构纪律" 节）

- 禁止任何"加一个 component 类型就改 mega-class 源码"的路径
- 禁止把 per-component UI 逻辑塞进任一 mega-class
- 禁止在 `EditorState` / 任一子 context 上无脑加字段
- 允许的反射形式：手写宏 + 模板特化的 Builder API；仍禁反射库
- lint：`scripts/check_invariants.py` 的 `editor-no-hardcode` 规则在 v0.2.5 完成（落 `tools/OrangeEditor/.schema-first-locked` marker）后切 error；之前是 warn-only

## Notes

- v0.2.5 之后任何编辑器新功能在开工前应先读本 ADR 与 CLAUDE.md "OrangeEditor 架构纪律" 节，确认抽象边界
- 若未来发现 schema 驱动在某个具体 component 上确实表现出工程负担（如：极特殊的 immediate-mode 交互组件），不允许回退到 hardcode；应通过 `IEditorInspectorPlugin` 覆写默认 schema 渲染解决，并在本 ADR 追加 "Notes / 实际遇到的例外" 段
- Cocos Creator 不参考（TS/Web 栈不兼容；详见 CLAUDE.md "OrangeEditor 参考引擎与资源" 节）
