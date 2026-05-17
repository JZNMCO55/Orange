# Architecture Decision Records (ADR)

本目录记录 OrangeEngine / OrangeEditor 演进过程中**非平凡**的架构决策。

## 什么进 ADR

凡满足以下任一条件的决策都应留 ADR：

- 在两个或更多明显方案之间做了**选择**（如：编辑器 D1 三种 tool-side model 映射；schema-first vs hardcode + fallback）
- 与 CLAUDE.md 既有 invariant 有**张力或扩展**（如：禁用反射库 vs 允许手写宏 Builder API）
- 事后追评一项**失误**（如：v0.1 Inspector hardcode 选择是失误，v0.2.5 整骨纠偏）
- 跨仓 / 跨 session 协同纪律（如：OrangeRender 单 session 双向操作禁令）

不进 ADR 的：日常 task 完成记录（去 commit message / design-plan ✅）、bug 修复（commit message）、纯 typo / refactor。

## 文件命名

`ADR-<3 位序号>-<kebab-case-slug>.md`

序号单调递增，**不复用、不重排**。被取代的 ADR 用 frontmatter `status: superseded-by: ADR-NNN` 链向后继，正文保留作为历史。

## 模板

```markdown
---
id: ADR-NNN
title: <一句话决策标题>
status: accepted        # proposed | accepted | superseded-by: ADR-NNN | deprecated
date: YYYY-MM-DD
deciders: [<人 / session 标识>]
related:
  - docs/editor-roadmap.md
  - vendor/Orange-Wiki/wiki/concepts/.../*.md
---

## Context（为什么要做这个决策）

<触发场景：是谁在做什么时撞上的；当时的约束 / 信息 / 误解。>

## Options Considered（候选方案）

1. **方案 A** —— <一句话>。优点 / 缺点。
2. **方案 B** —— ...

## Decision（选了哪个 + 一句话理由）

<明确说选了哪个、为什么不选其他、关键 trade-off。>

## Consequences（影响）

- **正面**：<...>
- **负面 / 待还的债**：<...>
- **强制 invariant**（如有）：<同步沉淀到 CLAUDE.md 哪一节>

## Notes（可选：参考文献 / 反对意见 / 历史链接）
```

## 与其他文档的关系

| 文件 | 角色 |
|------|------|
| `docs/design-plan.md` | Phase 1–5.5 task 级历史（**做了什么**） |
| `docs/roadmap.md` | Phase 6+ 前瞻路线 |
| `docs/editor-roadmap.md` | OrangeEditor 路线 + D1/D5 节内嵌当时的决策快照 |
| `docs/engine-known-gaps.md` | 编辑器 / sample 撞上的引擎缺口（**遇到了什么**） |
| `docs/decisions/` (本目录) | 跨文件 / 跨阶段的**架构决策**（**为什么这么选**） |
| `CLAUDE.md` | invariant + 工作流纪律（**之后必须遵守什么**） |

ADR 与 roadmap 内嵌决策的关系：**roadmap 是计划，ADR 是决策**。同一件事可同时被两处提到——roadmap 里写当前 milestone 怎么落，ADR 里写为什么这么选。如果某个 roadmap 内嵌决策很关键且会被后续 milestone 反复引用，把它提取到 ADR；否则留在 roadmap 即可。

## Index

| ID | 标题 | 状态 | 日期 |
|----|------|------|------|
| [ADR-001](ADR-001-editor-schema-first-and-no-hardcode.md) | OrangeEditor 转向 schema-first 架构 + 全局禁止 hardcode | accepted | 2026-05-12 |
| [ADR-002](ADR-002-editor-visual-system.md) | OrangeEditor 视觉体系决策（EditorTheme token + Cocos 灰 + 橙 accent + Codicons + 4px 色带） | accepted | 2026-05-17 |
