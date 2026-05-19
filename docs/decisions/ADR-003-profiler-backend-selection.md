---
id: ADR-003
title: OrangeEditor v0.9 Profiler 后端选型 —— 自实现 AutoProfile RAII（暂不接 Tracy）
status: accepted
date: 2026-05-19
deciders: [solo-dev]
related:
  - docs/editor-roadmap.md
  - cmake/Dependencies.cmake
  - vendor/Orange-Wiki/wiki/techniques/debugging/in-game-profiler.md
  - vendor/Orange-Wiki/wiki/concepts/foundations/debug-tooling.md
  - vendor/Orange-Wiki/wiki/sources/game-engine-architecture-ch10.md
  - vendor/LumixEngine/src/core/profiler.h
  - vendor/LumixEngine/src/editor/profiler_ui.h
---

## Context

OrangeEditor v0.9 milestone 的关键 deliverable 之一是 **Profiler 面板**。`docs/editor-roadmap.md:494-506` 的描述给了两个候选路径："接 `ORANGE_ENGINE_WITH_TRACY` 或自实现 in-game profiler（wiki `techniques/debugging/in-game-profiler.md`）"。两条路径在投入 / 调试摩擦 / 长期演进上差别显著，决策必须在 c0 commit 落地前拍板，否则后续 c3-c6 的 instrumentation 代码会按某一路径写死，事后切换成本翻倍。

### 当前状态（2026-05-19 reconfirm）

- **Tracy gate 仅 CMake 骨架**：`cmake/Dependencies.cmake:94` 声明 `option(ORANGE_ENGINE_WITH_TRACY "Link Tracy as the profiler backend" OFF)`；`CMakeLists.txt:527` 在 ON 时 `find_package(Tracy CONFIG REQUIRED)` + `target_compile_definitions(orange_engine PRIVATE ORANGE_ENGINE_WITH_TRACY=1)`。**引擎 `src/` + `include/` 全仓 grep `tracy::` / `TracyZone` / `ZoneScoped` 零命中**——纯 CMake stub，没有任何 instrumentation 接通。
- **自实现路径已有 wiki 完整算法层 + C++ 参考实现**：`vendor/Orange-Wiki/wiki/techniques/debugging/in-game-profiler.md`（AutoProfile RAII + 分层 sample bin + inclusive/exclusive 时间 + CSV 导出）+ `concepts/foundations/debug-tooling.md` §10.2 § 整体调试工具体系。来源 `sources/game-engine-architecture-ch10.md`（Gregory 2018 第 10 章）。
- **参考引擎 LumixEngine 走自实现 + Tracy 兼容双轨**：`vendor/LumixEngine/src/core/profiler.{h,cpp}` 是 Lumix 自己的 sample bin + frame timeline 后端；同时通过宏在 Tracy ON 时把 zone 同步推给 Tracy。证明双轨可行但工程量翻倍。
- **v0.8 c2 已铺好 in-game 面板基础**：Console 面板（接 `Core::Log` SetLogSink）已经验证了"引擎产生数据 → 编辑器 ImGui 面板消费 + filter + scroll"的端到端节奏；Profiler 面板沿用同款节奏即可。

### 约束

- CLAUDE.md "Serialization and reflection" 节 / ADR-001 都明确禁反射库；自实现 profiler 不引入新反射依赖（仅 `Core::Profiler` 公共面手写 API），与现有 invariant 兼容。
- CLAUDE.md "Header isolation" 节：Tracy 头如果接通会涉及多模块 PRIVATE 链接（Render / Physics / VfxSystem / Scene 都要插 `ZoneScoped`），引入新的跨模块 include 表面；自实现 `Core::Profiler` 是 engine 模块层级最低的 Core，公共面无第三方 include，干净。
- v0.8 c5 自承诺的"完整 `(Component&, EditorAssetContext&)` 签名整骨留 v0.9" 是顺带项；本 ADR 不涉及。

## Options Considered

### 方案 A：接 Tracy 作为唯一 Profiler 后端

把 `ORANGE_ENGINE_WITH_TRACY` 默认切 ON（或定义为 v0.9 硬依赖），引擎主循环 + Render / Physics / VfxSystem / Scene update 关键热点全数插 `ZoneScoped` 宏；编辑器 ProfilerPanel 用 Tracy 网络协议读 + 自绘 ImGui UI，或干脆告诉用户"打开 Tracy 客户端 GUI"。

- **优点**：工业级 profiler，flame graph / GPU sample / lock contention / memory tracker 全套现成；零自己写 timeline 渲染代码
- **缺点**：
  - v0.9 deliverable 描述 "Profiler 面板"——若让用户去开外部 Tracy 客户端，**严格说不是"编辑器内 panel"**，违背 milestone 字面要求
  - Tracy `find_package` + 链接增加 build 摩擦；Debug 配置启动期开销不小（Tracy 后台线程 + 共享内存 / TCP socket）
  - 跨模块插 `ZoneScoped` 需要 Render / Physics / VfxSystem / Scene **都引入 Tracy include**——Header isolation 多了一个跨横线
  - 内存统计 per-module 这一项 Tracy 提供，但口径与"按引擎模块切分"对不上（Tracy 按 alloc-callstack 聚合，不按 engine 模块语义聚合）；要么自定义 marker 改造，要么本项 deliverable 单独走自实现路径——**等于双轨**

### 方案 B：自实现 AutoProfile RAII（wiki 路径） + ImGui 面板

按 `vendor/Orange-Wiki/wiki/techniques/debugging/in-game-profiler.md` 完整算法层 + C++ 参考实现落 `Core::Profiler`：AutoProfile RAII + 静态声明的 sample bin 树（含 parent 关系）+ 帧末 `finalize_frame()` 计算 exclusive time + 重置 call_count。Editor 端 ProfilerPanel 走 ImGui 树形展示 inclusive/exclusive ms + call count + 帧耗时柱状图（plot lines） + 内存统计 tab。

- **优点**：
  - 完全 in-game / in-editor，与 v0.9 deliverable "Profiler 面板" 字面对齐
  - 与 v0.8 c2 Console 面板节奏一致（engine 数据源 → editor ImGui 面板消费），架构连续
  - `Core::Profiler` 是 engine 内最低层模块，公共面零第三方 include；不增加 Header isolation 表面
  - 内存统计 per-module 用同一套 sample bin 注册路径（按 module category 注册 alloc tracker），口径统一
  - wiki 推荐路径 + Lumix `src/core/profiler.h` 验证可行
- **缺点**：
  - 算法实现 + ImGui 面板自绘工作量约 2-3 个 commit（c3 backend + c5 panel + c6 memory tab）
  - 多线程 instrumentation 留 future（v0.9 单线程主循环够用；Phase 9+ job system 上线时再扩 thread-local sample bin）
  - 没有 GPU profiling / lock contention 等高级 feature——但 v0.9 milestone 描述里本来也不要求

### 方案 C：双轨 —— 自实现为默认 + Tracy 作 macro 兼容层（Lumix 同款）

`ORANGE_PROFILE_SCOPE("name")` 宏在 `ORANGE_ENGINE_WITH_TRACY=ON` 时展开成 `ZoneScopedN(name)` + 自实现 store；OFF 时仅自实现路径。Editor 端 ProfilerPanel 始终走自实现数据源；Tracy 是给"想用外部 flame graph 工具"的开发者准备的可选增强。

- **优点**：长期上限高（未来发现自实现某项不够用时，Tracy 在那条路径接上即可）；兼顾两边
- **缺点**：**工程量是方案 B 的 ~1.5 倍**——同样要先把自实现完整落地，再加一层宏适配；v0.9 milestone 内推不动；本质上方案 B 的超集，可未来用 superseding ADR 升级到 C

## Decision

**采用方案 B**：自实现 AutoProfile RAII（按 wiki `techniques/debugging/in-game-profiler.md` 路径）作为 v0.9 Profiler 唯一后端。Tracy gate（`ORANGE_ENGINE_WITH_TRACY` CMake option）**保留 stub 不动**——既不切默认值，也不在 v0.9 期接通——为未来 superseding ADR-XXX 升级到方案 C 留接口。

### 选择 B 而非 A 的理由

1. **milestone deliverable 字面对齐**：v0.9 描述要 "Profiler 面板"，方案 A 让用户去开外部 Tracy 客户端不算 "面板"
2. **架构连续性**：与 v0.8 c2 Console 面板（engine 数据源 → editor ImGui 面板）同款节奏，认知负担低
3. **invariant 兼容性更好**：`Core::Profiler` 零第三方 include；方案 A 的 `ZoneScoped` 跨 Render / Physics / VfxSystem / Scene 多模块插宏会引入新的跨横线

### 选择 B 而非 C 的理由

1. **v0.9 不需要 Tracy 提供的高级 feature**（GPU sample / lock contention），不为未必使用的能力付工程量
2. **C 是 B 的超集**——B 落地后未来若真撞上自实现不够用，superseding ADR 升级到 C 成本可控（宏层加一行 `ZoneScopedN`）；提前做 C 是过度设计

## Consequences

### 正面

- v0.9 ProfilerPanel 与 ConsolePanel / SettingsPanel / KeybindingsPanel 一起构成 OrangeEditor "in-engine 工具面板" 风格统一的栈
- 无新增第三方依赖，build 摩擦 / install 包大小 / 启动期开销零增加
- `Core::Profiler` 公共面 + 一组宏（`ORANGE_PROFILE_SCOPE` / `ORANGE_PROFILE_DECLARE_BIN`）足够覆盖 v0.9 instrumentation 需求
- 内存统计 per-module 与帧耗时统计共用 sample bin 注册路径，UI 节奏统一

### 负面 / 待还的债

- 多线程 instrumentation 留 future（v0.9 单主循环没问题；Phase 9+ job system 上线时需要扩 thread-local sample bin + lock-free 聚合）
- 没有 GPU profiling（vkCmdWriteTimestamp 包装）——若 v1.x Ori-like 视觉子模式撞上 GPU 瓶颈，按需开独立 milestone 加 GPU profiler 子系统
- Tracy gate（`ORANGE_ENGINE_WITH_TRACY` CMake option）变成"宣告但不实施"状态，文档要说明这一点避免未来开发者误以为可以直接打开就有效果——本 ADR + `cmake/Dependencies.cmake:91-94` 注释为此

### 强制 invariant（沉淀路径）

- `Core::Profiler` 公共面纳入 Header isolation 检查表（公共头零第三方 include；任何 `<tracy/...>` 出现属 lint 违规）
- v0.9 落地后在 CLAUDE.md "Common commands" 段补一条 "in-game profiler 启动方式 = 编辑器内 View → Profiler 打开面板"（与 Console 节奏一致）
- 若未来 superseding ADR 升级到方案 C：`ORANGE_PROFILE_SCOPE` 宏改 dual-dispatch（自实现 + Tracy）；引擎模块开始 include Tracy 时本 ADR `status` 切 `superseded-by: ADR-XXX`，正文保留作历史

## Notes

- Lumix `vendor/LumixEngine/src/core/profiler.h` 是同栈最近的双轨实现参考——若未来走方案 C 升级，直接对照其宏 dispatch 路径
- wiki `in-game-profiler.md` 给的 C++ 参考实现来源是 Gregory 2018 §10.8；OrangeEngine 落地时按 wiki 算法层重写，**不直接复制 wiki 内 C++ 片段**——遵守 wiki 消费纪律（wiki 是算法 / 思想参考，不是源码库）
- Cocos Creator 不参考（TS/Web 栈，profiler 实现路径与 C++ 不可类比）
- v0.8 c5 自承诺的"完整 `(Component&, EditorAssetContext&)` 签名整骨"延后到 v0.9 c7 commit 处理，与本 ADR 正交
