# Milestone Start Checklist

任何 OrangeEngine / OrangeEditor 的 milestone（design-plan task / editor-roadmap v0.x / engine-known-gaps 缺口）**开工前**走一遍。约 5–10 分钟，目的是把"边写边发现要返工"的成本前置到读文档阶段。

## 通用步骤

1. **定位 milestone 的权威描述**
   - 引擎主线：`docs/design-plan.md` 中对应 Task
   - 编辑器：`docs/editor-roadmap.md` 中对应 v0.x milestone
   - 引擎缺口响应：`docs/engine-known-gaps.md` 中对应 GAP 条目
   - 渲染器需求：`vendor/OrangeRender/docs/incoming_feature.md` 中对应 FEATURE
   - 确认：输入 / 输出 / 影响模块 / 前置 / 验收**清单**，所有项都看懂了

2. **查 Orange-Wiki**
   - 从 `vendor/Orange-Wiki/wiki/index.md` 起步
   - 找 subsystem / concept / technique 页：本 milestone 涉及到的核心概念是否有专页
   - 跟 `prerequisites` / `see_also` 展开 1–2 层
   - 输出：能在心里举出 2–3 个相关 wiki 页相对路径，准备在最终答复中引用

3. **查相关 ADR**
   - `docs/decisions/README.md` 索引
   - 本 milestone 是否触碰已有 ADR 的约束？是否需要新建 ADR？
   - 若是新建 ADR：先写 Context + Options + Decision 草稿，再开 code（避免事后追编造正当性）

4. **查参考引擎（仅编辑器 milestone）**
   - 首要：`vendor/LumixEngine/src/editor/` 的同名 / 同类 feature（C++ / ImGui 同栈，最有效）
   - 思想：`vendor/godot/editor/` 对应文件（异栈但成熟，看架构思想）
   - 资源：`vendor/cocos-engine/editor/assets/` 是否有可用占位资源
   - 输出：能在心里举出 1–2 个 vendor 内文件相对路径

5. **跑 invariant lint baseline**
   ```
   python scripts/check_invariants.py
   python scripts/check_claude_md_drift.py
   ```
   - 必须当前**全绿**才开始动改动；如果原本就红，先单独 session 修，不在本 milestone 内捎带
   - 修后再跑一次确认 baseline 干净

6. **检查跨仓影响**
   - 本 milestone 是否需要 OrangeRender 提供新能力？→ 停下来，登记到 `vendor/OrangeRender/docs/incoming_feature.md`，结束本 session，按 CLAUDE.md "单 session 双向操作禁令" 另开 session 处理
   - 本 milestone 是否触发 Orange-Wiki 补页？→ 完成后另开 wiki 维护 session 补
   - 本 milestone 是否触发 engine-known-gaps 登记？→ 同上，**登记不实现**

7. **写 commit-plan 草稿**
   - 这个 milestone 大概拆成几个 commit？每个 commit 的边界是什么？
   - 不需要写死，目的是确认改动是"可拆分的小步" vs "一坨"
   - 后者通常意味着 milestone 拆得不够细，回 design-plan / editor-roadmap 检查

## 触发拒绝开工的红线

任一条命中，**不开工**：

- 步骤 1 中发现 milestone 描述与 `docs/` 其他文件冲突（如 design-plan 说前置已完成但 editor-roadmap 说没）→ 先调和文档
- 步骤 5 中 lint baseline 红 → 先修
- 步骤 6 中发现需要 OrangeRender 新能力 → 另开 session，本 session 仅登记需求
- 步骤 7 中发现无法拆成清晰小 commit → milestone 太粗，先拆

## 触发更新本 checklist 的情形

- 新增工作流工具（如新增一个 lint 类别 / 新增一个外部参考 repo） → 在通用步骤里加一行
- 发现某条"通用步骤"长期被跳过且没事故 → 重新评估是否真的通用
