# Orange Engine

Windows-first C++20 game framework，承载基于 `OrangeEngine::orange_engine` 的多款游戏（含首款 Ori-like 平台跳跃）。以静态库形态分发，通过 `find_package(OrangeEngine CONFIG)` 被消费方 `OrangeGames` monorepo 使用。

## 从这里开始读

| 你想做的事 | 起点 |
|---|---|
| 第一次接触本项目，想理解整体工作流与文档组织 | [`../Orange-Wiki/case-studies/orange-engine/onboarding.md`](../Orange-Wiki/case-studies/orange-engine/onboarding.md)（**强烈推荐先读**） |
| 当下整体状态 + 编码 / 架构纪律 | [`CLAUDE.md`](CLAUDE.md) |
| Phase 1–5.5 历史任务表 | [`docs/design-plan.md`](docs/design-plan.md) |
| Phase 6+ 长期路线 | [`docs/roadmap.md`](docs/roadmap.md) |
| 编辑器演进路线 | [`docs/editor-roadmap.md`](docs/editor-roadmap.md) |
| 架构决策（ADR）索引 | [`docs/decisions/README.md`](docs/decisions/README.md) → Wiki [`case-studies/orange-engine/decisions/`](../Orange-Wiki/case-studies/orange-engine/decisions/) |

## 仓库 constellation（sibling 拓扑，[ADR-009](../Orange-Wiki/case-studies/orange-engine/decisions/ADR-009-vendor-topology-inversion.md)）

本仓是 [Orange-Ecosystem](https://github.com/JZNMCO55/Orange-Ecosystem) umbrella 仓持有的 4 个 sibling submodule 之一。生态总览：

| 仓 | 角色 | 与本仓关系 |
|---|---|---|
| **OrangeEngine**（本仓）| 引擎本体 + 编辑器（OrangeEditor）| —— |
| **OrangeRender** | Vulkan 渲染器 | sibling `../OrangeRender/`，`find_package(OrangeRender CONFIG)` 消费 |
| **Orange-Wiki** | 生态中央知识库（branch `Orange-Render-Wiki`）| sibling `../Orange-Wiki/`，知识 + 项目档案 + 跨项目反哺三层 |
| **OrangeGames** | 多游戏 monorepo（consumer）| sibling `../OrangeGames/`，通过 `find_package(OrangeEngine CONFIG)` 反向消费本仓 |

典型物理布局：

```
Orange-Ecosystem/
├─ OrangeEngine/   ← 本仓
├─ OrangeRender/
├─ Orange-Wiki/
└─ OrangeGames/
```

本仓单独 clone 不能直接 build —— 需把 sibling 摆好或通过 `CMAKE_PREFIX_PATH` 指向 OrangeRender install。完整说明见 `CLAUDE.md` "Toolchain" 节。建议直接 `git clone --recursive https://github.com/JZNMCO55/Orange-Ecosystem.git` 一次性拿到全生态。

完整工作流与跨仓纪律见上文 onboarding 文档。
