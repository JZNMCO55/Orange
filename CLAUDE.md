# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 回答语言

**所有回答必须使用中文。** 无论用户用何种语言提问，所有面向用户的文字输出（解释、说明、分析、建议等）均使用中文书写。代码标识符、专业术语（API 名称、库名、命令行参数等）保留英文原文不翻译。

## Project

OrangeEngine is a Windows-first, C++20 game framework targeting 2D / 2.5D games (first product: an Ori-like 平台跳跃 with a fluid/slime protagonist that swallows boss forms). It is delivered as a static library `OrangeEngine::orange_engine` (switchable to dll via `BUILD_SHARED_LIBS=ON`) intended for third-party integration through `find_package(OrangeEngine CONFIG)`. The engine owns scene representation, asset management, animation runtimes, physics, audio, input, and the application main loop; it does **not** own gameplay, narrative, level data, or game-specific systems (those live in the **OrangeGames monorepo**，承载多款基于本引擎的游戏 / demo，含首款 Ori-like).

The engine sits on top of `OrangeRender` (a Vulkan renderer also developed in this constellation) and consumes `Orange-Wiki` (a curated game-engine knowledge base) as its primary reference. 自 2026-05-24 [ADR-009](../Orange-Wiki/case-studies/orange-engine/decisions/ADR-009-vendor-topology-inversion.md) 起，OrangeEngine / OrangeRender / Orange-Wiki / OrangeGames 四仓由 [Orange-Ecosystem](https://github.com/JZNMCO55/Orange-Ecosystem) umbrella 仓唯一持有为 sibling submodule（本仓 `vendor/` 内不再持有 OrangeRender / Orange-Wiki，DragonBones 等纯运行时第三方依赖保留）。

- Current version: `0.1.0` (Unreleased; 0.x ABI is **not** stable)
- Status (基准 2026-05-22)：**Phase 1 ~ 5.5 全 ✅** + **Phase 6.5 · 渲染真实感基线（PBR + IBL）整体 ✅**（Task 06.5-01 ~ 07 全 ✅；2026-05-19 跨仓 session 落地 GAP-2026-05-19-pbr-ibl-specular-quality（multi-scatter compensation + prefilter 4096 sample）+ GAP-2026-05-19-editor-environment-component-wiring（.hdr 浏览器 + Pipeline 自动 re-bake）+ 顺路 fix BUG-2026-05-18-vma-shutdown-allocation-leak-assertion 双源；B.1 + B.2 acceptance-checklist 已迁 `../Orange-Wiki/case-studies/orange-engine/milestones/phase-6.5/`（ADR-006 落地后）；samples 13_pbr_direct + 14_pbr_ibl 已编出 + 视觉验收通过）+ **Phase 6 · OrangeEditor 工具链整体 ✅**（v1.0 验收 2026-05-22 通过，详见 `../Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-checklist.md`；OrangeEditor 自己按 semver 独立演进，CMake VERSION 0.0.2 → 1.0.0 与 roadmap 概念对齐；v0.1 / v0.1.5 / v0.2 / v0.2.5 / v0.3 / v0.4 / v0.4.5 / v0.5 / v0.6 / v0.6.5 / v0.8 / v0.8.5 / v0.9 / v0.9.5 / v1.0 全部 ✅；v0.7 Animation 子模式留待状态机图编辑器需求实际触发；v1.x 长尾如 Hot reload / ACP / C# 脚本 / 地形 等按拉动触发，不在 v1.0 critical path 上）。下一阶段进 Phase 7+，前瞻路线见 `docs/roadmap.md` 与 ADR-001 / ADR-003 / ADR-004（已迁 `../Orange-Wiki/case-studies/orange-engine/decisions/`，入口 `docs/decisions/README.md`）。`docs/design-plan.md` 的 ✅ 标记是 phase 进度的权威 source；本节描述若与之冲突，以 design-plan.md 为准。任何 commit 修改了 design-plan / editor-roadmap 的 ✅ 状态后，跑 `python scripts/check_claude_md_drift.py` 确认本节没有新漂移
- Authoritative documents (read these first):
  - `docs/design-plan.md` — Phase 1–5.5 architecture + task table（含 ✅ 进度）
  - `docs/roadmap.md` — Phase 6+ long-term roadmap（含 Phase 6.5 outline 入口）
  - `docs/editor-roadmap.md` — OrangeEditor v0.x 路线（独立 semver）
  - `docs/engine-known-gaps.md` — 编辑器 / sample 撞上的引擎缺口登记
  - `../Orange-Wiki/case-studies/orange-engine/reference/extension-points.md` — public API extension surface and project-level invariants（已迁 Wiki；`docs/extension-points.md` 留重定向 stub）
  - `../Orange-Wiki/case-studies/orange-engine/reference/coding-standards.md` — naming, guards, API macro (delta vs OrangeRender)（已迁 Wiki；`docs/coding-standards.md` 留重定向 stub）
  - `docs/milestone-start-checklist.md` — 任意 milestone 开工前的 5–10 分钟 ritual
  - `docs/milestone-end-checklist.md` — 任意 milestone 标 ✅ 前的 5–10 分钟 ritual
  - `docs/decisions/README.md` — Architecture Decision Records 入口（实际 ADR 已迁 `../Orange-Wiki/case-studies/orange-engine/decisions/`，本 README 维护索引 + "什么进 ADR" 节）
- Archived references（已迁 Wiki，仅历史回顾时翻阅）:
  - `../Orange-Wiki/case-studies/orange-engine/milestones/phase-6.5/milestone-design.md` — Phase 6.5 PBR+IBL 详细 milestone 设计（原 `docs/pbr-ibl-milestone.md`）
  - `../Orange-Wiki/case-studies/orange-engine/pre-game-design/character-forms.md` — first-game design note（原 `docs/case-studies/character-forms.md`）
  - `../Orange-Wiki/case-studies/orange-engine/milestones/` — 历次 milestone acceptance checklist（编辑器 v0.x + Phase 6.5 + Phase 3）
  - `../Orange-Wiki/case-studies/orange-engine/historical-architecture/4-layer-archive/` — 一年前 GEA 风格 4-layer 死架构（原 `docs/Technical Documentation/`）

## Toolchain

- Windows 11, MSVC 2022, CMake 3.28+, C++20 (extensions OFF).
- Renderer dependency: `OrangeRender 0.1.x` via `find_package(OrangeRender CONFIG REQUIRED)`. OrangeRender 按 [ADR-009](../Orange-Wiki/case-studies/orange-engine/decisions/ADR-009-vendor-topology-inversion.md) 走 sibling 拓扑 —— 典型布局为 `<repo>/../OrangeRender/`（在 Orange-Ecosystem umbrella 下），或通过显式 `CMAKE_PREFIX_PATH` 指向已 install 的 OrangeRender SDK（如 `D:/sdk/orange-render`）。本仓 `vendor/` 内**不再**持有 OrangeRender。
- Knowledge-base dependency: `Orange-Wiki` at sibling `../Orange-Wiki/` — **must be on branch `Orange-Render-Wiki`** (default `main` does not contain the engine wiki content). 同样按 ADR-009 走 sibling 拓扑，本仓 `vendor/` 内**不再**持有 Orange-Wiki。
- Required third-party (resolved via `find_package`, expected under a single prefix such as `D:\3rdparty`):
  - Inherited transitively from OrangeRender: `Vulkan`, `glfw3`, `glm`, `volk`, `VulkanMemoryAllocator`
  - Engine-direct PUBLIC: `EnTT`, `nlohmann_json`
  - Engine-direct PRIVATE: `Box2D` (3.x), `miniaudio`, `stb_image`, `stb_truetype`, DragonBones C++ runtime
  - Engine-direct PUBLIC（消费者可见）: `Dear ImGui`（FetchContent vendored 为 `OrangeEngine::imgui`，PUBLIC 暴露给游戏侧 `Layer::OnImGui()` 写 debug-UI；自 GAP-2026-05-27-consumer-imgui-tuning-hook 起从 PRIVATE 升 PUBLIC，决策见 ADR；编辑器复用同一份不再自 vendor）
- Optional: `spdlog` (gated by `ORANGE_ENGINE_WITH_SPDLOG`, default OFF until Phase 1 / Task 04 wires Core::Log against it), `tracy` (gated by `ORANGE_ENGINE_WITH_TRACY`, default OFF).
- The LunarG Vulkan SDK must be installed with `VULKAN_SDK` set (transitively required by OrangeRender).

## Common commands

> Phase 1 ~ 5.5 全 ✅；顶层 `CMakeLists.txt` 已是引擎构建入口，下面的命令是**现行**用法（不是目标态）。

Bootstrap third-party deps:

```
python scripts/fetch_and_build_3rdparty.py                 # default prefix D:\3rdparty
python scripts/fetch_and_build_3rdparty.py --prefix E:\deps --jobs 16
```

Manual configure / build once deps are installed:

```
cmake -S . -B build -DCMAKE_PREFIX_PATH="D:/3rdparty;D:/sdk/orange-render"
cmake --build build --config Debug -j
```

Run the minimal sample (verifies window + main loop):

```
build/bin/Debug/01_minimal_window.exe
```

Tests live under `tests/` and are gated behind `ORANGE_ENGINE_BUILD_TESTS` (default OFF):

```
cmake -S . -B build -DCMAKE_PREFIX_PATH="..." -DORANGE_ENGINE_BUILD_TESTS=ON
cmake --build build --config Debug -j
ctest --test-dir build -C Debug --output-on-failure
```

## Architecture

The engine is organized **horizontally by module**, not as a vertical pyramid. Strict invariants govern which module may import which third-party headers.

```
┌───────────────────────────────────────────────────────────┐
│  Game repository (find_package(OrangeEngine))             │
│   game-side components / systems / shaders / assets       │
└───────────────────────────────────────────────────────────┘
                           │ uses public API
                           ▼
┌───────────────────────────────────────────────────────────┐
│  OrangeEngine (this repository)                           │
│   include/orange/engine/...    Public API                 │
│   src/...                      Private implementation     │
│   src/render/                  ★ ONLY directory allowed   │
│                                  to #include <orange/...> │
│   src/animation/dragonbones/   ★ ONLY DragonBones region  │
│   src/physics/box2d/           ★ ONLY Box2D region        │
│   src/audio/miniaudio/         ★ ONLY miniaudio region    │
│   src/script/dotnet/           ★ ONLY CLR hosting region  │
└───────────────────────────────────────────────────────────┘
                           │ find_package(OrangeRender)
                           ▼
┌───────────────────────────────────────────────────────────┐
│  OrangeRender (sibling ../OrangeRender，ADR-009)          │
│   Application / RenderFramework / RenderGraph / RHI       │
│   Vulkan backend (volk + VMA)                             │
└───────────────────────────────────────────────────────────┘
```

### Modules

- **Core** (`include/orange/engine/core/`): Result / Handle / Time / Hash / Log / `Serialization` (JsonReader/Writer + BinaryReader/Writer + SchemaVersion) / `Config`.
- **Platform** (`include/orange/engine/platform/`): Window (GLFW wrapped — public API never exposes `GLFWwindow*`) / FileSystem / Clock / raw input.
- **App** (`include/orange/engine/app/`): AppHost / Layer / LayerStack / FrameContext / AppConfig.
- **Asset** (`include/orange/engine/asset/`): AssetHandle / AssetRegistry / IAssetLoader / built-in loaders for Mesh / Texture / Shader / Skeleton / Sound / Font.
- **Scene** (`include/orange/engine/scene/`): World (EnTT registry wrapper) / Entity / TransformComponent / HierarchyComponent / NameComponent / ISystem.
- **Render** (`include/orange/engine/render/`): Camera / RenderableComponent / LightComponent / Material / MaterialInstance / MaterialSystem / Pipeline / PostProcessChain / VfxSystem. **The only module that may consume OrangeRender headers**.
- **Animation** (`include/orange/engine/animation/`): IAnimator (abstract) / SkeletalAnimator (DragonBones backend) / ProceduralAnimator (shader-uniform driven) / AnimationStateMachine / AnimatorRegistry. Dual-backend by design from the start.
- **Physics** (`include/orange/engine/physics/`): PhysicsWorld / RigidBodyComponent / ColliderComponent / ColliderDesc + `ReplaceFixture` runtime API.
- **Audio** (`include/orange/engine/audio/`): AudioEngine (miniaudio wrapped) / Sound / SoundInstance.
- **Input** (`include/orange/engine/input/`): Action / ActionMap / InputContext (stack-based).
- **Save** (`include/orange/engine/save/`, **Phase 5.5**): SaveGameRegistry / SaveGameSystem.

### Phase discipline

Engine work is organized into Phases 1 → 5.5 (then Phase 6+ in `docs/roadmap.md`). **Do not implement Phase N+1 features while Phase N is incomplete.** Each Phase has a demonstrable milestone (typically a `samples/` executable). Phase status（基准 2026-05-22；权威 source 是 `docs/design-plan.md` / `docs/roadmap.md` / `docs/editor-roadmap.md` 中各 Task 的 ✅ 标记）：

- **Phase 1 ~ 5.5：全部 ✅**（design-plan.md Task 级均已 ✅；`samples/01_minimal_window` ~ `samples/09_vfx_demo` 已落地）
- **Phase 6 · OrangeEditor 工具链：✅**（v1.0 验收 2026-05-22 通过；v0.1 ~ v0.9.5 + v1.0 全部 ✅，详见 `docs/editor-roadmap.md` + `../Orange-Wiki/case-studies/orange-engine/milestones/editor/editor-v1.0-acceptance-checklist.md`；OrangeEditor 按独立 semver 演进，CMake VERSION = 1.0.0；v0.7 Animation 子模式留待状态机图编辑器需求拉动；v1.x 长尾按需触发，不在 v1.0 critical path 上）
- **Phase 6.5 · 渲染真实感基线（PBR + IBL）：✅**（roadmap.md Task 06.5-01 ~ 07 全 ✅；samples 13_pbr_direct + 14_pbr_ibl 视觉验收通过；B.1 / B.2 acceptance-checklist 已迁 `../Orange-Wiki/case-studies/orange-engine/milestones/phase-6.5/`）
- **Phase 7+**：未开工；前瞻路线见 `docs/roadmap.md`

When working on a task, locate it in `docs/design-plan.md` Task Breakdown（Phase 1–5.5 历史 + 接口参考）、`docs/editor-roadmap.md`（Phase 6 编辑器）或 `docs/roadmap.md`（Phase 7+）. Implement only the listed outputs; reject scope creep.

## Coding conventions (enforced)

The full rule set is OrangeRender's `coding-standards.md` (linked from `../OrangeRender/docs/coding-standards.md`). OrangeEngine differences are documented in `docs/coding-standards.md`. Highlights:

- Namespace everything in **`Orange::Engine`** (nested per module: `Orange::Engine::Render`, `Orange::Engine::Scene`, `Orange::Engine::Animation`, `Orange::Engine::Physics`, etc.). Top-level common types (`AppHost`, `World`, `Entity`, `Layer`, `FrameContext`) live in `Orange::Engine` directly.
- Functions and types: PascalCase. Locals/params: camelCase.
- Member data: `m` + camelCase (`mFrameIndex`). Member pointers: `mp` + camelCase (`mpDevice`).
- Static class data: `s` + camelCase; static class pointer members: `sp` + camelCase. File-scope / function-local statics also use `s` prefix.
- Non-member pointers: `p` + camelCase (`pData`, `pNext`).
- Every header uses `#ifndef`/`#define`/`#endif` guards named `ORANGE_ENGINE_<UPPER_PATH>_H` (e.g. `include/orange/engine/scene/World.h` → `ORANGE_ENGINE_SCENE_WORLD_H`). Do not use `#pragma once`.
- Public exported types use `ORANGE_ENGINE_API` (analogous to OrangeRender's `ORANGE_API`).
- Brace style: opening brace on its own line, aligned with the declaration (matches OrangeRender's examples).
- Comments: terse — only *why* / non-obvious traps / decisions; drop lines that restate the code. ≤2-3 lines per point.
- Variable names: meaningful, no cryptic abbreviations (on top of the prefix rules above).
- Namespace body: indent one level (contents track nesting); use collapsed `namespace A::B::C {}` to stay single-level. New code only — existing flat-namespace code is not retrofitted.

> 2026-07-03 用户新增风格约定（注释精炼 / 变量名达意 / namespace 体缩进），going-forward，遇冲突优先于旧 `coding-standards.md`。namespace 缩进有别于 Google/LLVM「不缩进」主流惯例，属用户明确选择。

## Design guardrails (project-level invariants)

These are not suggestions. Violating them is treated as an architectural bug.

### Header isolation
- **Public headers** (`include/orange/engine/**/*.h`) must NOT include any of:
  - `<orange/...>` (OrangeRender RHI / RenderGraph headers)
  - `<box2d/...>`, `<box2d.h>`
  - `<dragonBones/...>`
  - `"miniaudio.h"`
  - `<nethost.h>`, `<hostfxr.h>`, `<coreclr_delegates.h>` (and any CLR hosting header)
  - `<vulkan/...>`, `<volk.h>`, `<vk_mem_alloc.h>`
- **`<orange/...>` is allowed only in `src/render/**`**. If you need OrangeRender from another module, you are doing it wrong — route through Render's public API.
- **`<box2d/...>` is allowed only in `src/physics/box2d/**`**.
- **`<dragonBones/...>` is allowed only in `src/animation/dragonbones/**`**.
- **`"miniaudio.h"` is allowed only in `src/audio/miniaudio/**`**.
- **`<nethost.h>` / `<hostfxr.h>` / `<coreclr_delegates.h>` (CLR hosting headers) are allowed only in `src/script/dotnet/**`** (ADR-017). The public façade `include/orange/engine/script/` must not expose any CLR / hostfxr type (PIMPL hides the hostfxr handle).

### Game-specific concepts forbidden in engine
- Engine code must not contain identifiers like `Slime`, `Boss`, `Player`, `Form`, or any other first-game term. If a feature is needed only for the first game, it belongs in the game repository as a custom component / system / shader, consumed via the engine's extension points.

### Serialization and reflection
- **No reflection libraries.** Phase 1–6 forbids `entt::meta`, RTTR, cereal-with-reflection, or any AST codegen. All `Read` / `Write` functions are hand-written.
- **C# 脚本 tweakable 反射不破本禁令**（ADR-017）：`ScriptComponent` 的 Inspector tweakable 字段枚举发生在 **C# 托管侧（`System.Reflection`）/ 编辑器工具侧**，引擎 runtime C++ 仍**零反射库**——CLR host（`src/script/dotnet/`）只经 hostfxr 取托管函数指针，不在 C++ 引入任何反射依赖。
- **No bare `nlohmann::json` calls.** All serialization must go through `Core::Serialization` (`JsonReader` / `JsonWriter` / `BinaryReader` / `BinaryWriter`). The public API does not expose `nlohmann::json` types.
- **Every serializable type declares `SchemaVersion`.** The read path validates version before parsing.
- **A schema version that has shipped to a player or to the game repo never changes.** Add a new version + migrator instead.

### Phase scope
- The Render module's `Pipeline::InsertPass` exists from Phase 3 but is `assert(false, "not implemented before Phase 5")` until Phase 5 actually wires it.
- VFX, scene serialization, save game, GPU particles, custom shader hot reload are all post-Phase-3. Do not stub them in earlier.

### No task references in code
- Source files (headers and `.cpp`) must NOT reference task numbers, phase numbers, or planning artifacts in comments — no "Phase 1 / Task 07", no "see Task 09", no "added by Task NN", no "Critical Path" annotations. Existing task-referencing banners in earlier files were a one-off pattern and are not to be propagated to new work.
- Task / phase metadata belongs in `docs/design-plan.md`, `docs/roadmap.md`, commit messages, and PR descriptions — not in code, where it rots as task numbers shift (Phase 1 already renumbered once).
- Code comments are still welcome when they explain a non-obvious *why* (a constraint, an invariant, a workaround). They just may not anchor that *why* to a task identifier.

### 代码注释使用中文
- 新增或修改的源文件（头文件、`.cpp`）注释一律使用**中文**书写。专业术语（PBR、frustum culling、archetype、job system、unique_ptr、RAII、cache locality 等）保留英文，不强行翻译；中文术语首次出现时可括注英文，后续直接用英文术语。
- 不主动重写历史文件里现存的英文注释（既存代码维持原样，避免无意义 diff）；只有在实际改动到那段代码、注释本身需要更新时，才顺手把该处改为中文。
- 代码标识符（类名、函数名、变量名、命名空间）仍按 `coding-standards.md` 全部使用英文 PascalCase / camelCase；本规约只约束注释文本。
- 字符串字面量、日志消息默认仍用英文（避免编码与跨平台终端显示问题），除非该字符串是面向中文用户的 UI 文案。

### OrangeRender public API discipline
- The renderer is consumed via its public API (`Orange::Rhi::*` and the `OrangeRender::orange_render` target). Do not reach into `../OrangeRender/src/` from this repo. If OrangeRender lacks something you need, file an issue / patch in the OrangeRender repo, not a workaround here.
- **绝对不允许在同一个 session 内既向 OrangeRender 提 feature、又在本仓库消费 / 处理该 feature。** 提 feature（在 OrangeRender 仓库新增需求、改动其公共 API、bump 其版本以引入新能力）和在 OrangeEngine 这一侧基于该 feature 写代码，必须拆成两个独立的 session：先在一个 session 把 OrangeRender 的改动落地、合并、tag/commit 固化；再在新的 session 里在 Orange-Ecosystem umbrella 仓 bump `OrangeRender` submodule pointer 并消费（ADR-009 后本仓 `vendor/OrangeRender` 已移除，OrangeRender commit 锁版本由 Ecosystem 仓持有）。原因：同 session 双向操作会把"渲染器还没真正稳定的 API"当成既成事实写进引擎，下游一旦回退就会留下半成品；并且容易绕过 OrangeRender 自己的 review / wiki / 版本纪律。遇到"engine 这边发现 OrangeRender 缺东西"的情况，本 session 的正确动作是：停下当前 engine 任务，仅把需求记录下来（commit message / issue / 临时笔记），结束本 session，下次开新 session 再处理。
- **需求 / bug 提交入口**：发现 OrangeRender 缺能力 → 追加到 `../OrangeRender/docs/incoming_feature.md`；发现 OrangeRender bug → 追加到 `../OrangeRender/docs/incoming_bugs.md`。条目命名约定 `## FEATURE-<日期>-<slug>` / `## BUG-<日期>-<slug>`，便于后续 commit / PR title 引用。OrangeRender 维护者按其仓内 `CLAUDE.md` 的"外部需求 / Bug 处理工作流"评审 + 拆解 + 落地 + 归档，整个评审过程会就地在该条目内留痕；OrangeEngine 这边 **不要**直接编辑别人写的评审记录，也不要在落地前抢先 bump Ecosystem 那侧的 submodule pointer。需求落地 + tag 后，在新 session 里到 Orange-Ecosystem 仓 bump `OrangeRender` submodule pointer 到新 commit + sync 到本仓 working copy（pull 拿最新 sibling 内容），然后开始消费。
- **纯文档 / work-queue 登记跨仓豁免**（[ADR-010](../Orange-Wiki/case-studies/orange-engine/decisions/ADR-010-per-session-doc-edit-exemption.md)）：上述"双向操作禁令"与 per-session 单子仓纪律**仅约束代码**。同一 session 内向多个 sibling 仓的 work queue 登记需求 / bug（`../OrangeRender/docs/incoming_feature.md`、`docs/engine-known-gaps.md` 等）或编辑纯文档（README / wiki 页 / design notes / ADR），**不受单子仓限制**——登记需求本身就是"不实现不消费"的解耦动作。仍禁止：同 session 改两仓**代码** / 提 feature 代码又消费 / 在消费方文档把未落地能力当既成事实。机械约束：跨仓文档编辑仍各仓分别 commit + Ecosystem bump pointer。

## Knowledge base: Orange-Wiki

The wiki at `../Orange-Wiki/` is a curated, incrementally-built knowledge base on game engine construction. It is mounted as a Claude Code skill via `../Orange-Wiki/SKILL.md`. **It is the project's authoritative reference for engine implementation decisions.**

### When to consult it
- Implementing or designing any engine subsystem (rendering, ECS, physics, animation, audio, asset, scheduling)
- Choosing an algorithm (culling strategy, shadow technique, allocator, scheduling model)
- Architectural tradeoffs (archetype vs sparse-set ECS, immediate vs retained mode, push vs pull constants, etc.)
- Cross-engine reference questions ("how does Unreal/Unity/Bevy/Godot do X")

### How to consult it
1. Always start with `../Orange-Wiki/wiki/index.md` — it catalogs every page.
2. Pick candidate pages (subsystem / concept / technique / engine / comparison / pattern).
3. Read self-contained summaries first to filter relevance.
4. Follow `prerequisites` and `see_also` frontmatter for related context.
5. **In answers to the user, cite specific wiki pages with relative paths**: e.g., "按 `../Orange-Wiki/wiki/concepts/ecs/archetype-storage.md`，archetype 在 add component 时整行迁移……"

### What NOT to do
- **Do not modify wiki content** from this repository as a side effect of consumer queries. Wiki maintenance is governed by `../Orange-Wiki/CLAUDE.md` and happens in that repo's own session.
- Do not skip the wiki and answer from training-set knowledge for engine-construction topics.
- Do not assume a topic is covered — if `index.md` does not list a relevant page, say so explicitly and recommend ingesting a source rather than fabricating details.

### Branch
- The wiki must be on branch `Orange-Render-Wiki`. The default `main` branch does not contain the ingested content. Verify with `git -C ../Orange-Wiki branch --show-current`.

## OrangeEditor 参考引擎与资源

OrangeEditor 开发时采用**双参考**策略：

### LumixEngine — 技术实现参考

`vendor/LumixEngine/` 是 OrangeEditor 各 feature 的**首要技术参考**。Lumix 是 C++ / ImGui 技术栈，架构与 OrangeEditor 高度相似（in-engine editor、ImGui dock、ECS world、命令栈）。

**使用方式**：
- 开发每个编辑器 feature 前，先在 `vendor/LumixEngine/src/editor/` 找对应实现，理解其架构选择和数学方案，再以 OrangeEditor 自身的约定重新实现（**不直接复制代码**，许可证 / 命名 / 架构三方面都有差异）
- Lumix 的做法同时指导 OrangeEngine 和 OrangeRender 的**能力补齐方向**——编辑器侧发现引擎 / 渲染器缺能力时，先对比 Lumix 如何在自己渲染器里解决，再按既有工作流向 `docs/engine-known-gaps.md` / `../OrangeRender/docs/incoming_feature.md` 登记需求
- 如果 Orange-Wiki 尚未收录 Lumix 对应 feature 的 wiki 页，建议在完成实现后开一个 Orange-Wiki 维护 session 补页，后续同类 feature 可直接引用 wiki 而非重新 grep Lumix 源码

**关键文件索引（编辑器开发高频参考）**：

| 功能领域 | 文件 |
|----------|------|
| Gizmo 几何 + 碰撞检测 + 交互数学 | `src/editor/gizmo.cpp`（~934 行） |
| 命令栈 / MoveEntityCommand / merge | `src/editor/world_editor.cpp`（~3126 行） |
| SceneView + gizmo → 命令集成点 | `src/renderer/editor/scene_view.cpp` |
| Spline editor（自定义 gizmo 扩展示例）| `src/editor/spline_editor.cpp` |

### Cocos Creator — UI/UX 布局参考与占位资源

`vendor/cocos-engine/` 用于编辑器**界面布局和交互设计**参考，不作为技术实现参考。

- viewport 工具栏、底部 tab 容器（Assets / Console / Animation）、全局 toolbar 等布局设计参照 Cocos Creator 的界面组织方式
- Cocos 的美术资源（图标、UI 纹理、默认 mesh 等）可作为**临时占位素材**，后期统一替换为项目自有资源；使用前确认具体资源的许可证条款（Cocos 引擎本体 MIT，内置资源许可证需单独核查）
- **不参考 Cocos 的技术实现**（TypeScript / Web 技术栈与 OrangeEditor C++ 完全不同）

## OrangeEditor 架构纪律

**禁止 hardcode**。OrangeEditor 自 v0.2.5（架构整骨 milestone，见 `docs/editor-roadmap.md`）起进入 schema-first / plugin-first 架构。背景：v0.1 ~ v0.2 期"先把功能跑起来"的写法在 Inspector / Add-Component / EditorState 等多处沉淀成 hardcode + god class（9 个 `DrawInspectorXxx` 成员 + 19 字段 god struct），调研 Lumix（`src/editor/*` + `src/engine/reflection.h`）与 Godot（`editor/*` + `core/object/class_db.h`）确认 schema-first / plugin-first 是同栈工业标准 —— 因此把"禁止 hardcode"沉淀为项目级 invariant，与 Header isolation / Phase scope 同级严肃。

具体规约：

- **不允许**任何"加一个 component 类型就改 `EditorRenderLayer` / `EditorState` / 其他 mega-class 源码"的路径。新增 component 的 Inspector / Gizmo / Add-Component 菜单项必须通过 schema 注册或 `IEditor*Plugin` 注册完成
- **不允许**把 per-component 的 Inspector / Gizmo / 序列化 UI 逻辑塞进任一 mega-class（god class）；必须以独立注册项 / plugin / schema 形式存在
- **不允许**在 `EditorState` 上无脑加字段；新功能找对应子 context（`EditorSelection` / `EditorSceneContext` / `EditorAssetContext` / `EditorCameraState`，由 v0.2.5 拆出）加；没有合适 context 就先拆 context
- **允许**的反射形式：手写宏 + 模板特化的 Builder API（参 `vendor/LumixEngine/src/engine/reflection.h`、`vendor/godot/core/object/class_db.h`），编译期注册零运行时反射库依赖；**仍然禁止** `entt::meta` / RTTR / cereal-with-reflection / clang AST codegen（沿用 "Serialization and reflection" 节禁令）
- **DCC import 4 件套路径**（v1.1 ADR-008 落地）：新增任何外部资产格式（`.fbx` / `.dae` / `.usd` / `.exr` / `.ktx` / `.dds` 等）的 importer **必须**走相同路径：
  - (a) vendor 接 + 单 header 优先（参 `vendor/tinyobjloader/` + `vendor/cgltf/`，都是 in-tree single-header MIT）
  - (b) 转引擎自家二进制（`.mesh` v3 / `.texture` ORTX 等）+ copy 源到 `assets/<TypeDir>/`（`Models/` / `Textures/` / `Audio/` / 等）
  - (c) 同目录写 `.meta` JSON sidecar（schema_version + sourcePath + sourceHash + handleId + importParams{}）
  - (d) `AssetRegistry::Insert<T>()` / `Load<T>()` 入仓
  - importer 模块**必须**在 `tools/OrangeEditor/import/` 下（不污染 `src/asset/`，保持引擎 runtime 不带 importer 依赖）
  - .meta JSON schema 已 shipped 版本不改字段语义（沿用 "Serialization and reflection" 节"schema version 出厂即冻结"约束）
- 违反以上任一条视为编辑器侧架构 bug，与引擎侧 invariant 同等严肃，code review 应直接 block

发现现有代码触犯禁令时的正确动作：登记到 v0.2.5（若尚未开工）或后续整骨 milestone，**不**在当前任务里顺手 hack 一条新 hardcode 路径"先用着"——这正是 v0.1 ~ v0.2 期债务累积的方式。

## 工作流基础设施

按 2026-05-12 工作流升级（详见 ADR-001，已迁 `../Orange-Wiki/case-studies/orange-engine/decisions/ADR-001-editor-schema-first-and-no-hardcode.md`），项目沉淀了 5 件协同纪律基础设施（2026-05-14 补 end-checklist 后从 4 件扩到 5 件）。下面是**何时用 + 怎么用**——都是低摩擦工具，不用就会让早期决策腐烂。

### 1. Invariant lint —— `scripts/check_invariants.py`

机器化执行 CLAUDE.md 的硬纪律：header isolation / 公共头无裸 `nlohmann::json` / 代码注释无 `Task NN` `Phase N` / OrangeEditor 无 `DrawInspectorXxx` hardcode（v0.2.5 后切 error）。

**何时跑**：
- **每次 milestone 开工前**：跑一次确认 baseline 干净（如脏先修，不在本 milestone 内捎带）
- **每次 commit 前**：跑一次确认本次改动没引入新违规
- 用 `--write-baseline` 重新生成 `scripts/.invariants-baseline.json`——**仅在批量清理历史 banner 后才动**，日常不要拿它"压"违规

**接受的输出**：`All invariants OK. (N grandfathered by baseline)` —— 退出码 0；任何 `new violation(s) found` 必须修，不允许 commit。

### 2. CLAUDE.md 漂移检测 —— `scripts/check_claude_md_drift.py`

把 `docs/design-plan.md` ✅ 计数 + 文件系统真实状态（顶层 `CMakeLists.txt` / `scripts/fetch_and_build_3rdparty.py` / `Src/` 历史目录）和本文件的 "Phase status" 段比对。

**何时跑**：
- 任何 commit 修改了 design-plan.md / editor-roadmap.md 的 ✅ 标记后
- 任何 commit 修改了 CLAUDE.md 的 Project / Common commands / Phase discipline 段后
- 也可加进 CI 兜底（推荐）

**接受的输出**：`CLAUDE.md drift: none detected.` —— 退出码 0；任何漂移条目都意味着本文件需要更新。

### 3. ADR（架构决策记录）—— `docs/decisions/README.md` → `../Orange-Wiki/case-studies/orange-engine/decisions/`

跨阶段、跨文件的架构决策留 ADR；从 `ADR-001` 起，单调编号、不重排。**roadmap 写计划，ADR 写决策**——同一件事可同时在两处提到，但"为什么这么选" + "事后追评"只属于 ADR。ADR 文件 2026-05-21 起按 ADR-006 落地迁 Wiki，本仓 `docs/decisions/README.md` 保留索引 + "什么进 ADR" 节作为入口；新 ADR 同 session 写 Wiki 子树，本仓索引同 commit 更新。

**何时写新 ADR**：见 `docs/decisions/README.md` "什么进 ADR" 节——非平凡选择 / 与 invariant 张力 / 事后追评失误 / 跨仓协同纪律 四类。

**何时读 ADR**：milestone 启动 ritual 第 3 步（见下）；review 跨阶段改动时；编辑器 / 渲染器接口讨论时。先看 `docs/decisions/README.md` 索引 → 跳转 Wiki 对应 ADR 文件。

### 4. Milestone 启动 ritual —— `docs/milestone-start-checklist.md`

任何 design-plan task / editor-roadmap v0.x / engine-known-gaps 缺口开工**前**走一遍 5–10 分钟 checklist：定位权威描述 → 查 wiki → 查 ADR → 查参考引擎 → 跑 lint baseline → 检查跨仓影响 → 写 commit-plan 草稿。**红线触发不开工**条款见该文件末段。

设计意图：把"边写边发现要返工"的成本前置到读文档阶段，而不是在 commit 后才反应过来违反了哪条纪律。

### 5. Milestone 完工 ritual —— `docs/milestone-end-checklist.md`

任何 milestone **标 ✅ 之前**走一遍 5–10 分钟 checklist：跑 lint + drift → acceptance-checklist 文档就位 → 参考引擎 retro 对比（本期引入新机制时必做）→ ADR 决定 → 跨仓影响登记 → commit 序列回顾 → roadmap 标 ✅ → memory 沉淀。**红线触发不标 ✅** 条款见该文件末段。

设计意图（与 start-checklist 对偶）：把"标 ✅ 后才发现纪律漏洞"的成本前置到完工 ritual 阶段。2026-05-14 v0.3 milestone 标 ✅ 时漏写 acceptance-checklist 后补此文档；同 commit 落 v0.3 retro 节。

### 这 5 件的相互关系

```
milestone-start-checklist （前置：每个 milestone 开工前）
   │
   ├─ 第 3 步：读相关 ADR（入口 docs/decisions/README.md → Wiki case-studies/orange-engine/decisions/）
   ├─ 第 4 步：查参考引擎对照设计（Lumix / Godot / Cocos）
   ├─ 第 5 步：跑 invariant lint + drift 检测 → 必须 baseline 干净
   ├─ 第 6 步：跨仓影响识别 → 触发 engine-known-gaps / OrangeRender incoming_feature 登记
   └─ 第 7 步：写 commit-plan 草稿

milestone 进行中：
   ├─ 每个 commit 前跑 invariant lint
   └─ 撞上引擎缺口 / OrangeRender 需求 → 立刻登记，不在同 session 实现

milestone-end-checklist （前置：标 ✅ 之前）
   │
   ├─ 第 1 步：跑 invariant lint + drift 检测（baseline 全绿）
   ├─ 第 2 步：acceptance-checklist 文档就位（编辑器 milestone 必须）
   ├─ 第 3 步：参考引擎 retro 对比（本期引入新机制时必做，与 start 第 4 步对偶）
   ├─ 第 4 步：ADR 决定（与 start 第 3 步对偶）
   ├─ 第 5 步：跨仓影响登记（与 start 第 6 步对偶）
   ├─ 第 6 步：commit 序列回顾（与 start 第 7 步对偶）
   ├─ 第 7 步：roadmap 标 ✅ + 跑 drift 确认 CLAUDE.md 同步
   └─ 第 8 步：memory 沉淀（可选）
```

## Working in this repo: practical guidance

When given a task in this repo, default to this workflow:

1. **Read `docs/design-plan.md`** to confirm which Phase the task belongs to and what its outputs are. Reject scope that crosses Phase boundaries.
2. **Read `../Orange-Wiki/case-studies/orange-engine/reference/extension-points.md`** if the task touches any extension surface (custom components / shaders / asset types / render passes / animator backends / save game).（已迁 Wiki；`docs/extension-points.md` 留重定向 stub）
3. **Consult `../Orange-Wiki/`** for algorithmic / architectural decisions. Cite specific pages.
4. **Check the invariants in this file** before adding any `#include`. The header isolation rules are checked in code review.
5. **Match OrangeRender's coding conventions**. Header guards, naming, brace style, namespace nesting — all aligned.
6. **Add a sample** to `samples/` if the task introduces new public API. The sample is the canonical "does it still run end-to-end" check.

When uncertain whether something is engine-level vs game-level: **default to game-level**. The engine should remain genuinely reusable for hypothetical future games, not specialized for the first one.

### Task completion marking

After a task's outputs are landed and its acceptance criteria are met, append `✅` to the task heading in `docs/design-plan.md` (or `docs/roadmap.md` for Phase 6+ tasks). This matches OrangeRender's own convention and gives a quick visual scan of where the engine stands.

```markdown
#### Task 01：清理旧骨架并建立新目录结构 ✅
#### Task 02：建立顶层 CMake 与 `ORANGE_ENGINE_API` 宏机制 ✅
#### Task 03：第三方依赖接入与 vendor submodule 化 ✅
#### Task 04：定义 Core 基础类型
```

Rules:

- The mark goes on the task **heading line** only — never inside the task body, never on Phase headings (a Phase is "done" only when every Critical-Path task in it carries ✅).
- Only mark a task done when its **outputs exist on disk** (or are correctly staged) **and** its **验收标准 / 验证方式** has been observed (build green / sample runs / test passes). A staged file alone is not completion.
- If a task is partially done (e.g. files written but verification deferred because deps are missing), do **not** mark it ✅ yet. Use the chat / commit message to describe the deferred state and apply the mark once verification clears.
- When a previously-marked task gets reverted or its acceptance breaks, **remove the ✅** in the same change so the doc reflects current truth.
- Phase 1 task numbering shifted once (Task 05 was inserted as Core Serialization & Config, pushing the old 05–10 to 06–11). If similar restructurings happen, the ✅ marks must be migrated to the new numbering — do not let stale marks linger.

## File layout reference

```
CMakeLists.txt                          # top-level build (Phase 1 / Task 02)
include/orange/engine/                  # public API; namespace Orange::Engine
  OrangeEngine.h                          # umbrella header
  OrangeEngineExport.h                    # ORANGE_ENGINE_API macro
  OrangeEngineVersion.h                   # generated
  core/  platform/  app/  asset/  scene/
  render/  animation/  physics/  audio/  input/
  save/                                   # Phase 5.5
src/                                    # private implementation
  core/  platform/  app/  asset/  scene/
  render/                                 # ★ unique OrangeRender consumer
  animation/dragonbones/                  # ★ unique DragonBones consumer
  physics/box2d/                          # ★ unique Box2D consumer
  audio/miniaudio/                        # ★ unique miniaudio consumer
  input/  save/
samples/                                # end-to-end demos, one per Phase milestone
tests/                                  # ctest suite
cmake/                                  # toolchain + Config template
docs/                                   # authoritative architecture / roadmap / standards
vendor/
  DragonBones/                          # 引擎私有运行时依赖（保留 in-tree 集成）

# sibling 拓扑（ADR-009，2026-05-24）—— 不在本仓内，由 Orange-Ecosystem umbrella 持有
# ../OrangeRender/                      # vendored renderer (find_package source)
# ../Orange-Wiki/                       # knowledge base (branch: Orange-Render-Wiki)
# ../OrangeGames/                       # consumer 仓（多游戏 monorepo）
```

As of 2026-05-21 (ADR-006), historical / archive documents have moved to `../Orange-Wiki/case-studies/orange-engine/`:

- `historical-architecture/4-layer-archive/` — the abandoned GEA-style 4-layer architecture from a year ago (do not read as authoritative; preserved for history)
- `milestones/` — completed editor v0.x acceptance checklists, Phase 6.5 PBR+IBL acceptance, Phase 3 audio/light gate
- `decisions/` — Architecture Decision Records (ADR-001..N)
- `pre-game-design/` — first-game design notes
- `retrospectives/` — Phase / milestone retros (currently empty; populated when Phase 6 wraps)

All current architecture lives in `docs/design-plan.md` and its companion files.
