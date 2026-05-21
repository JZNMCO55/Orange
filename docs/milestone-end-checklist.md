# Milestone End Checklist

任何 OrangeEngine / OrangeEditor 的 milestone（design-plan task / editor-roadmap v0.x / engine-known-gaps 缺口）**完工标 ✅ 之前**走一遍。约 5–10 分钟，目的是把"标 ✅ 后才发现纪律漏洞"的成本前置到完工 ritual 阶段。

设计意图：v0.3 milestone 标 ✅ 时漏写 acceptance-checklist，用户当场指出——根因是 `milestone-start-checklist.md` 有开工 ritual 但**没有对偶的完工 ritual**。本 checklist 即补这个对偶缺口。

## 通用步骤

1. **跑 invariant lint baseline + drift 检测**
   ```
   python scripts/check_invariants.py
   python scripts/check_claude_md_drift.py
   ```
   - 必须**全绿**；任一条红 → 不标 ✅，先修
   - 这是完工的必要条件，**第一步跑**——避免在文档 / retro 工作上投入后才发现 baseline 烂掉

2. **acceptance-checklist 文档就位**（编辑器 milestone 必须；引擎 sample milestone 按需）
   - 自 2026-05-21（ADR-006）起新 checklist 直接写到 Wiki：`vendor/Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v<X.Y>-acceptance-checklist.md`；不在本仓 `docs/acceptance/` 新增文件
   - 沿用 Wiki 已迁的 `editor-v0.2.5` / `editor-v0.3` / `editor-v0.4` 格式
   - **精炼优先**：v0.3 / v0.4 验收时用户当场反馈"重复内容太多"——新 checklist 不要为每个 commit 重复写 build / 编辑器启动 / 选实体 / 保存 加载 等通用前置步骤；通用步骤抽到文档顶部"前置环境"节一次说清，每个 commit 验收点只写**与该 commit 相关的最小差异**。详见 memory `feedback_milestone_acceptance_checklist_concise`
   - 验收点语言：GUI 操作 + 视觉结果，给无代码能力测试人员看（参 memory `feedback_testing_instruction_audience`）
   - 只列编辑器可操作项：接口声明 / 未上线 feature / 纯内部整骨**不进**清单（参 memory `feedback_milestone_acceptance_checklist`）
   - 勾选框留空——用户手动点完节点回归后再 ✅

3. **参考引擎 retro 对比**（仅编辑器 milestone，且本期引入了**新机制 / 新抽象**时必做）
   - 列出本期引入的新机制 / 新抽象（典型：新 plugin 类型、新 schema 控件、新命令栈模式、新调度路径）
   - 每条对照 Lumix / Godot 同款，确认我方设计选择仍然合理（**不是**重新设计；是确认事后看仍合理）
   - 触发跳过条件：本期 milestone 是字段补完 / demo 补齐 / 纯 bug fix → 跳过 retro
   - 输出形式：写进 acceptance-checklist 末段 `## v<X.Y> retro · 参考引擎对比` 节，**或**独立 ADR
   - 选择标准：是否构成跨阶段决策？跨阶段 → ADR；本 milestone 局部 → checklist 末段

4. **ADR 决定**
   - 本 milestone 期间是否产生了"非平凡选择 / 与 invariant 张力 / 事后追评失误 / 跨仓协同纪律"四类决策？
   - 是 → 新建 ADR（参 `docs/decisions/README.md`）
   - 否 → 跳过；不为凑数写 ADR

5. **跨仓影响登记**
   - 本期撞上的引擎缺口是否登记到 `docs/engine-known-gaps.md`？
   - 本期撞上的 OrangeRender 需求是否登记到 `vendor/OrangeRender/docs/incoming_feature.md`？
   - 若有未登记 → 立刻登记，**不**在 milestone ✅ commit 内顺手实现（违反 "单 session 双向操作禁令"）

6. **commit 序列回顾**
   - 与开工时 `milestone-start-checklist` 第 7 步写的 commit-plan 草稿对比
   - 实际 commit 顺序 / 边界 / 数量是否与草稿匹配？
   - 偏差点登记进 acceptance-checklist 的 retro 节或 commit message
   - 偏差不一定是 bug——可能是开工时未预见的细节；记录用于校准下次 commit-plan 精度

7. **roadmap / design-plan 标 ✅**
   - design-plan milestone → `docs/design-plan.md` 对应 task heading 加 ✅
   - editor milestone → `docs/editor-roadmap.md` 对应 v0.x heading 加 ✅
   - 标完后跑 `python scripts/check_claude_md_drift.py` 确认 CLAUDE.md "Phase status" 段同步

8. **memory 沉淀**（可选）
   - 本 milestone 期间是否收到用户反馈 / 自己事后发现的纪律漏洞？
   - 是 → 写进 memory（typically 类型 `feedback`）；下次同款问题前置规避

## 触发拒绝标 ✅ 的红线

任一条命中，**不标 ✅**：

- 步骤 1 中 lint / drift 红 → 先修
- 步骤 2 中 acceptance-checklist 漏写 / 不完整 → 先补
- 步骤 3 中本期引入了新机制但 retro 没做 → 先对比
- 步骤 5 中跨仓影响未登记 → 先登记
- 步骤 7 标 ✅ 后 drift 检测发现 CLAUDE.md 漂移 → 先同步 CLAUDE.md

## 与 milestone-start-checklist 的对偶关系

| start-checklist 第 N 步 | end-checklist 第 N 步 | 闭环 |
|------------------------|---------------------|------|
| 5 lint baseline 全绿 | 1 lint baseline 全绿 | 开工 / 完工两端守不变量 |
| 4 查参考引擎对照设计 | 3 参考引擎 retro 复审 | 开工选型 / 完工追评 |
| 3 查 ADR 是否需要新建 | 4 ADR 决定 | 开工预防遗漏 / 完工补落 |
| 6 跨仓影响识别 | 5 跨仓影响登记 | 开工发现 / 完工归档 |
| 7 commit-plan 草稿 | 6 commit 序列回顾 | 开工预估 / 完工校准 |

开工 ritual 是 "把返工成本前置到读文档阶段"；完工 ritual 是 "把纪律漏洞前置到 ✅ 阶段"。两者共同守住 milestone 的入出口。

## 触发更新本 checklist 的情形

- 新增工作流工具 / 文档类别 → 在通用步骤里加一行
- 发现某条"通用步骤"长期被跳过且没事故 → 重新评估
- 用户当场指出某次完工漏的纪律项 → 检查本 checklist 是否该项已覆盖；未覆盖则补
