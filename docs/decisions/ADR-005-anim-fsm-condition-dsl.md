---
id: ADR-005
title: AnimationStateMachine condition 可序列化形态 —— 引擎侧 ConditionExpr + StateContext parameter table（方案 B）
status: proposed
date: 2026-05-19
deciders: [solo-dev]
related:
  - docs/editor-roadmap.md
  - docs/decisions/ADR-001-editor-schema-first-and-no-hardcode.md
  - include/orange/engine/animation/AnimationStateMachine.h
  - tools/OrangeEditor/AnimFsmModel.h
  - tools/OrangeEditor/AnimFsmFileIO.h
  - vendor/Orange-Wiki/wiki/techniques/animation/animation-state-machine.md
---

## Context

v0.7 c2 状态机图编辑器（c2-1 ~ c2-6 已落地）需要让 transition condition **可序列化**到 `.anim_fsm` 文件，并在编辑器 UI 内可编辑。

### 当前状态

- 引擎运行时 `AnimationStateMachine::AddTransition(from, to, ConditionFn)` 的 `ConditionFn = std::function<bool(const StateContext&)>` —— 不可 introspect，不可序列化（见 `include/orange/engine/animation/AnimationStateMachine.h:57`）
- 编辑器 `.anim_fsm` schema v1.0（c2-2 落地）暂未引入 condition 字段，UI 内 transition 仅有 from / to 没有触发条件
- v0.7 编辑器路线图 deliverable："Skeletal animation state machine **图编辑**（节点 = state，边 = transition + **condition**），保存到 .anim_fsm" —— condition 编辑是必交付项

### 关键约束

1. 序列化要求：condition 必须能落盘到文本 / 二进制；`std::function` 无 API 反射出参数捕获
2. CLAUDE.md "Serialization and reflection" 节禁令仍生效：不能引入 `entt::meta` / RTTR / cereal-with-reflection / clang AST codegen
3. 跨仓 session 自由度：本 session 用户显式 `/goal` 允许 OrangeEngine 公共面改动（OrangeRender 单 session 双向禁令对本 ADR 不适用——本决策只动 OrangeEngine 自身）
4. 游戏侧 condition 注入路径：游戏代码可能直接用 `AddTransition(lambda)` 不走 .anim_fsm 文件——必须保留旧 API 向后兼容

### 与参考引擎对照

- **Unity Animator Controller**：condition = (parameter, op, threshold) 三元组；parameter table（Bool/Int/Float/Trigger）由 Animator 持有；transition 上多个 condition AND 组合
- **Lumix Engine**（`src/animation/controller.cpp`）：blend tree 模型，condition 同样是 paramName + op + value 的 plain data；编辑器侧序列化 + 运行时 evaluate 同套数据结构
- **Unreal AnimGraph**：BlueprintNode 图 + parameter 集合，结构更复杂但根本理念一致：condition 是数据，不是 callback

工业惯例一致指向：**condition 应该是引擎层的可序列化数据结构，而非 callback**。`std::function` 是 OrangeEngine v0.1 ~ v0.6 期的快速路径选择（"避免引入专用 condition DSL"，见 AnimationStateMachine.h:15-17 头注释），但编辑器图编辑需求触发后这条路径不再 viable。

## Options Considered

### 方案 A：编辑器侧 `EditableCondition` DSL + 加载时桥到 `std::function`

仅编辑器侧定义 `{paramName, op, value}` plain data；`.anim_fsm` 文件落盘这个结构；加载到运行时由编辑器（或中间层）以这些 data 生成 `std::function` 注入引擎 `AddTransition(ConditionFn)`。引擎公共面零改动。

- **优点**：
  - 不动 OrangeEngine 公共面，最小侵入
  - 旧 API `AddTransition(ConditionFn)` 是单一入口路径，避免 API 表面碎片化
- **缺点**：
  - **DSL 语义在编辑器侧重复实现**：游戏代码若想从 C++ 直接消费 `.anim_fsm`（不通过编辑器加载），必须重新写一遍 DSL → lambda 翻译层
  - **参数 table 也只在编辑器侧存在**：游戏代码每帧更新参数（`SetBool("isMoving", true)`）找不到引擎层入口
  - 工业惯例偏离：Unity / Unreal / Lumix 都把 parameter table 放在引擎层
- **适用场景**：跨仓 session 不允许 / 引擎正赶其它 critical path / 编辑器与游戏侧严格隔离

### 方案 B：引擎侧 `ConditionExpr` 数据结构 + `StateContext` parameter table

引擎公共面增加：
- `ParameterType` enum（Bool / Int / Float / Trigger）+ `Parameter` struct（type + variant value）
- `ConditionExpr` struct（paramName + op + threshold）
- `AddTransition(from, to, std::vector<ConditionExpr>)` 重载（多 condition AND 组合）
- `SetParameterBool / SetParameterInt / SetParameterFloat / SetTrigger` 运行时 setter
- `StateContext` 扩字段：指向当前 parameter table 的指针

旧 `AddTransition(ConditionFn)` 重载**保留**（向后兼容），游戏侧已有 lambda-driven 代码零改动。

编辑器侧 `EditableTransition` 加 condition 字段、`EditableStateMachine` 加 parameters 集合；`.anim_fsm` schema bump v1.0 → v1.1。

- **优点**：
  - 工业惯例一致（Unity / Unreal / Lumix 同款数据结构 + parameter table 在引擎层）
  - 游戏代码可直接消费 `.anim_fsm` 文件：加载到 EditableStateMachine → 引擎层 `AddTransition(ConditionExpr)` 翻译，编辑器不必参与运行时路径
  - parameter table 是 engine-level service：游戏代码 / UI / cinematic 都可调 `SetParameter*` 驱动状态机
  - Wiki `vendor/Orange-Wiki/wiki/techniques/animation/animation-state-machine.md` 描述的 Flat-Weighted 架构与本方案天然对位
- **缺点**：
  - 引擎公共面变更：AnimationStateMachine.h 新增 ~5 类型 + ~6 公共方法
  - tests/animation/AnimationStateMachineTest.cpp 需扩 ConditionExpr 覆盖（向后兼容旧测试保持）
- **不在范围**（避免方案 B 过度扩张）：
  - **Layered FSM** / blend tree —— Wiki 已明确 Orange 走 Flat 架构，留 v1.x 按需扩
  - **复合 condition 表达式**（OR / NOT / 嵌套）—— 一条 transition 多条 ConditionExpr 走 AND 组合即足够覆盖 Unity 90% 用例；OR 用 "复制 transition + 不同 condition" 模拟
  - **运行时 parameter table 反射 / introspect** —— 编辑器调 GetParameterNames() 拿名字列表即可，不引入完整 reflection

## Decision

**采用方案 B**。

### 理由

1. **工业惯例一致**：Unity / Unreal / Lumix 同栈编辑器都把 condition + parameter table 放在引擎层。编辑器侧 DSL（方案 A）是"假装引擎不存在"的局部解，长期会让游戏侧消费 `.anim_fsm` 变成另一条 DSL 翻译层
2. **单一真相源**：condition + parameter 都是引擎层 first-class 概念，编辑器只是它们的可视化编辑视图
3. **跨仓 session 显式授权**：用户 `/goal` 已明确允许本 session 跨仓改 OrangeEngine（OrangeRender 单 session 双向禁令对本决策不适用——本决策只触及 OrangeEngine 自身）
4. **向后兼容**：旧 `AddTransition(ConditionFn)` 保留，所有现有 lambda-driven 调用零改动；新 API 是 additive，不破历史代码

### 不在范围（明确排除）

- **Layered FSM** —— Wiki 明确 Orange 走 Flat；layered 是后续 IK / additive milestone 范围
- **blend tree** —— 同上
- **复合 condition 表达式（OR/NOT/嵌套）** —— AND 列表 + transition 复制可模拟，Unity / Lumix 也只 AND 列表
- **运行时 parameter introspect API** —— 编辑器只需读 transitions / parameters 名字列表，不需要完整反射

## Consequences

### 正面

- `.anim_fsm` schema 完整可序列化（v1.1）：transition 含 condition list，state machine 含 parameter list
- 游戏代码可不依赖编辑器消费 `.anim_fsm`：加载到 EditableStateMachine → 翻译到引擎 `AddTransition(ConditionExpr)` 即可
- parameter table 是 engine-level service：游戏 gameplay 代码 / cinematic 时间线 / UI debug 工具都可调 `SetParameter*` 驱动状态机
- 编辑器侧 UI 落 Inspector 中"选中 transition 显示 condition 编辑面板" / "Parameters 段管理参数列表"两条路径与 Unity Animator Controller 体验对齐
- Wiki `animation-state-machine.md` 描述的 Flat-Weighted 架构与运行时数据结构完整对应（之前 condition = std::function 是头注释的"刻意接受的代价"，本 ADR 消除该代价）

### 负面 / 待还的债

- AnimationStateMachine.h 公共面增 ~80 行（5 类型 + 6 方法）—— 一次性成本
- tests/animation/AnimationStateMachineTest.cpp 需扩 ~50 行 ConditionExpr 覆盖
- ConditionExpr `threshold` 用 `std::variant<bool, std::int32_t, float>` —— 序列化时按 type 分支选 variant 索引
- ConditionOp 枚举闭集（If / IfNot / Greater / Less / Equal / NotEqual / GreaterEqual / LessEqual）—— 不在 enum 内的运算符（contains / regex 等）不支持；如未来撞上再扩 enum

### 强制 invariant（新沉淀）

无新增项目级 invariant。本 ADR 在 ADR-001 "schema-first" 框架内属"可序列化数据形态"决策，不改变 OrangeEditor 架构纪律节的任何禁令；CLAUDE.md "Serialization and reflection" 节关于"反射库禁令"保持 —— `ConditionExpr` / `Parameter` 是手写 POD + variant，非反射库。

## Notes

- 参数 table 由 `AnimationStateMachine` 自身持有（不放 AnimatorComponent / IAnimator 上层）——状态机是参数的天然消费者，setter 路径最短；如未来发现需要跨多个 state machine 共享 parameter（layered FSM 触发），再向 Animator 上一层挪
- Trigger 类型语义：`SetTrigger("attack")` 设为 true；Tick 内 condition fire 后**自动 reset 为 false**（与 Unity Animator Trigger 一致）。这是为避免一次按键持续触发多次 transition
- v0.7 c2-7-A 落 ADR-005 status: proposed + 引擎侧数据结构 + AddTransition 重载 + 测试；c2-7-B 落编辑器 schema v1.1 + UI + 命令；v0.7 整体 ✅ 时切 status: accepted
- 升级到 layered FSM / blend tree 的 trigger 条件：第一款游戏明确需要 IK 叠加 / 多层动画混合 / 多 clip 同时播放 —— 不在 v0.7 ~ v1.0 critical path 上
