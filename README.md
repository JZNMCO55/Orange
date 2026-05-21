# Orange Engine

Windows-first C++20 game framework，目标作品：Ori-like 2.5D 平台跳跃。以静态库 `OrangeEngine::orange_engine` 形态分发，通过 `find_package(OrangeEngine CONFIG)` 被消费方游戏仓使用。

## 从这里开始读

| 你想做的事 | 起点 |
|---|---|
| 第一次接触本项目，想理解整体工作流与文档组织 | [`vendor/Orange-Wiki/case-studies/orange-engine/onboarding.md`](vendor/Orange-Wiki/case-studies/orange-engine/onboarding.md)（**强烈推荐先读**） |
| 当下整体状态 + 编码 / 架构纪律 | [`CLAUDE.md`](CLAUDE.md) |
| Phase 1–5.5 历史任务表 | [`docs/design-plan.md`](docs/design-plan.md) |
| Phase 6+ 长期路线 | [`docs/roadmap.md`](docs/roadmap.md) |
| 编辑器演进路线 | [`docs/editor-roadmap.md`](docs/editor-roadmap.md) |
| 架构决策（ADR）索引 | [`docs/decisions/README.md`](docs/decisions/README.md) → Wiki [`case-studies/orange-engine/decisions/`](vendor/Orange-Wiki/case-studies/orange-engine/decisions/) |

## 仓库 constellation

- **OrangeEngine**（本仓）— 引擎本体 + 编辑器（OrangeEditor）
- **OrangeRender** — Vulkan 渲染器（submodule `vendor/OrangeRender/`，`find_package` 消费）
- **Orange-Wiki** — 生态中央知识库（submodule `vendor/Orange-Wiki/`，branch `Orange-Render-Wiki`）

完整工作流与跨仓纪律见上文 onboarding 文档。
