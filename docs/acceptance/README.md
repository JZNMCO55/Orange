# Milestone Acceptance Checklists —— 入口

本目录在 2026-05-21 起空目录 + 单 README 入口。具体 acceptance checklist 已按 ADR-006 落地迁入 Wiki：

> **存放位置**：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/`
> **入口规则**：milestone ✅ 后**同 session** 写新 acceptance checklist 到 Wiki 对应子树（`milestones/editor/`、`milestones/phase-X/` 等），同 commit 在 OrangeEngine 本仓 `milestone-end-checklist.md` 第 2 步引用 Wiki 路径；不再在本 `docs/acceptance/` 目录新增文件。

## 已迁 Wiki 的 checklist 索引

### 编辑器 milestone（`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/`）

- editor-v0.2.5-acceptance-checklist.md（schema-first 整骨）
- editor-v0.3-acceptance-checklist.md
- editor-v0.4-acceptance-checklist.md
- editor-v0.4.5-acceptance-checklist.md（DPI 自适应）
- editor-v0.5-acceptance-checklist.md（Asset 浏览器 + Material 子模式）
- editor-v0.6-acceptance-checklist.md
- editor-v0.6.5-acceptance-checklist.md
- editor-v0.7-acceptance-checklist.md
- editor-v0.8-acceptance-checklist.md（与 Phase 6.5 整体 ✅ 同 session）
- editor-v0.8.5-acceptance-checklist.md
- editor-v0.9-acceptance-checklist.md（Profiler — ADR-003）
- editor-v0.9.5-acceptance-checklist.md（Schema AssetRef — ADR-004）

### Phase 6.5 · PBR + IBL（`vendor/Orange-Wiki/case-studies/orange-engine/milestones/phase-6.5/`）

- milestone-design.md（Phase 6.5 详细 milestone 设计，原 `docs/pbr-ibl-milestone.md`）
- phase-6.5-B.1-acceptance-checklist.md（PBR direct lighting）
- phase-6.5-B.2-acceptance-checklist.md（IBL + 视觉验收 6 项）

### Phase 3 · Audio + Point Light + Aux Passes Gate（`vendor/Orange-Wiki/case-studies/orange-engine/milestones/phase-3/`）

- audio-and-point-light-and-aux-passes-gate-acceptance-checklist.md（2026-05-20 落地）

## 与 milestone-end-checklist 的关系

`docs/milestone-end-checklist.md` 第 2 步"acceptance-checklist 文档就位"现仍有效，**但**：
- 新 checklist 直接写到 Wiki 子树（不在本目录新增）
- 沿用先前格式（参考 Wiki 已迁的 editor-v0.2.5 / v0.3 / v0.4 等）
- "精炼优先"等纪律不变（见 memory `feedback_milestone_acceptance_checklist_concise`）
