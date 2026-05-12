# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 回答语言

**所有回答必须使用中文。** 无论用户用何种语言提问，所有面向用户的文字输出（解释、说明、分析、建议等）均使用中文书写。代码标识符、专业术语（API 名称、库名、命令行参数等）保留英文原文不翻译。

## Project

OrangeEngine is a Windows-first, C++20 game framework targeting 2D / 2.5D games (first product: an Ori-like 平台跳跃 with a fluid/slime protagonist that swallows boss forms). It is delivered as a static library `OrangeEngine::orange_engine` (switchable to dll via `BUILD_SHARED_LIBS=ON`) intended for third-party integration through `find_package(OrangeEngine CONFIG)`. The engine owns scene representation, asset management, animation runtimes, physics, audio, input, and the application main loop; it does **not** own gameplay, narrative, level data, or game-specific systems (those live in the consumer game repository).

The engine sits on top of `OrangeRender` (a Vulkan renderer also developed in this constellation) and consumes `Orange-Wiki` (a curated game-engine knowledge base) as its primary reference.

- Current version: `0.1.0` (Unreleased; 0.x ABI is **not** stable)
- Status: **pre-Phase-1**. Architecture documents are complete; implementation has not started. The legacy `Src/` skeleton (GEA-style 4-layer cathedral) is scheduled for removal in Phase 1 / Task 01. Until then, the only authoritative artifact is `docs/`.
- Authoritative documents (read these first):
  - `docs/design-plan.md` — architecture, module breakdown, Phase 1 task table, Phase 2–5.5 task outlines
  - `docs/roadmap.md` — Phase 6+ long-term roadmap
  - `docs/extension-points.md` — public API extension surface and project-level invariants
  - `docs/coding-standards.md` — naming, guards, API macro (delta vs OrangeRender)
  - `docs/case-studies/character-forms.md` — first-game design note (NOT engine spec; will migrate to game repo when forked)

## Toolchain

- Windows 11, MSVC 2022, CMake 3.28+, C++20 (extensions OFF).
- Renderer dependency: `OrangeRender 0.1.x` via `find_package(OrangeRender CONFIG REQUIRED)`. OrangeRender is checked out at `vendor/OrangeRender/`.
- Knowledge-base dependency: `Orange-Wiki` at `vendor/Orange-Wiki/` — **must be on branch `Orange-Render-Wiki`** (default `main` does not contain the engine wiki content).
- Required third-party (resolved via `find_package`, expected under a single prefix such as `D:\3rdparty`):
  - Inherited transitively from OrangeRender: `Vulkan`, `glfw3`, `glm`, `volk`, `VulkanMemoryAllocator`
  - Engine-direct PUBLIC: `EnTT`, `nlohmann_json`
  - Engine-direct PRIVATE: `Box2D` (3.x), `miniaudio`, `stb_image`, `stb_truetype`, `Dear ImGui`, DragonBones C++ runtime
- Optional: `spdlog` (gated by `ORANGE_ENGINE_WITH_SPDLOG`, default OFF until Phase 1 / Task 04 wires Core::Log against it), `tracy` (gated by `ORANGE_ENGINE_WITH_TRACY`, default OFF).
- The LunarG Vulkan SDK must be installed with `VULKAN_SDK` set (transitively required by OrangeRender).

## Common commands

> **Note**: Until Phase 1 / Task 02 lands, there is no top-level `CMakeLists.txt` for the new structure. The legacy `CMakeLists.txt` references empty skeletons under `Src/` and is **not** the engine build entry. Do not invoke it. The commands below describe the **target** state after Phase 1 / Task 02.

Bootstrap third-party deps (target state — script does not exist yet, will arrive in Phase 1 / Task 03):

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
└───────────────────────────────────────────────────────────┘
                           │ find_package(OrangeRender)
                           ▼
┌───────────────────────────────────────────────────────────┐
│  OrangeRender (vendor/OrangeRender)                       │
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

Engine work is organized into Phases 1 → 5.5 (then Phase 6+ in `docs/roadmap.md`). **Do not implement Phase N+1 features while Phase N is incomplete.** Each Phase has a demonstrable milestone (typically a `samples/` executable). Phase status as of this writing:

- **Phase 1** (engine skeleton + minimal main loop): not started
- Phases 2–5.5: not started; outlines only

When working on a task, locate it in `docs/design-plan.md` Task Breakdown. Implement only the listed outputs; reject scope creep.

## Coding conventions (enforced)

The full rule set is OrangeRender's `coding-standards.md` (linked from `vendor/OrangeRender/docs/coding-standards.md`). OrangeEngine differences are documented in `docs/coding-standards.md`. Highlights:

- Namespace everything in **`Orange::Engine`** (nested per module: `Orange::Engine::Render`, `Orange::Engine::Scene`, `Orange::Engine::Animation`, `Orange::Engine::Physics`, etc.). Top-level common types (`AppHost`, `World`, `Entity`, `Layer`, `FrameContext`) live in `Orange::Engine` directly.
- Functions and types: PascalCase. Locals/params: camelCase.
- Member data: `m` + camelCase (`mFrameIndex`). Member pointers: `mp` + camelCase (`mpDevice`).
- Static class data: `s` + camelCase; static class pointer members: `sp` + camelCase. File-scope / function-local statics also use `s` prefix.
- Non-member pointers: `p` + camelCase (`pData`, `pNext`).
- Every header uses `#ifndef`/`#define`/`#endif` guards named `ORANGE_ENGINE_<UPPER_PATH>_H` (e.g. `include/orange/engine/scene/World.h` → `ORANGE_ENGINE_SCENE_WORLD_H`). Do not use `#pragma once`.
- Public exported types use `ORANGE_ENGINE_API` (analogous to OrangeRender's `ORANGE_API`).
- Brace style: opening brace on its own line, aligned with the declaration (matches OrangeRender's examples).

## Design guardrails (project-level invariants)

These are not suggestions. Violating them is treated as an architectural bug.

### Header isolation
- **Public headers** (`include/orange/engine/**/*.h`) must NOT include any of:
  - `<orange/...>` (OrangeRender RHI / RenderGraph headers)
  - `<box2d/...>`, `<box2d.h>`
  - `<dragonBones/...>`
  - `"miniaudio.h"`
  - `<vulkan/...>`, `<volk.h>`, `<vk_mem_alloc.h>`
- **`<orange/...>` is allowed only in `src/render/**`**. If you need OrangeRender from another module, you are doing it wrong — route through Render's public API.
- **`<box2d/...>` is allowed only in `src/physics/box2d/**`**.
- **`<dragonBones/...>` is allowed only in `src/animation/dragonbones/**`**.
- **`"miniaudio.h"` is allowed only in `src/audio/miniaudio/**`**.

### Game-specific concepts forbidden in engine
- Engine code must not contain identifiers like `Slime`, `Boss`, `Player`, `Form`, or any other first-game term. If a feature is needed only for the first game, it belongs in the game repository as a custom component / system / shader, consumed via the engine's extension points.

### Serialization and reflection
- **No reflection libraries.** Phase 1–6 forbids `entt::meta`, RTTR, cereal-with-reflection, or any AST codegen. All `Read` / `Write` functions are hand-written.
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
- The renderer is consumed via its public API (`Orange::Rhi::*` and the `OrangeRender::orange_render` target). Do not reach into `vendor/OrangeRender/src/` from this repo. If OrangeRender lacks something you need, file an issue / patch in the OrangeRender repo, not a workaround here.
- **绝对不允许在同一个 session 内既向 OrangeRender 提 feature、又在本仓库消费 / 处理该 feature。** 提 feature（在 OrangeRender 仓库新增需求、改动其公共 API、bump 其版本以引入新能力）和在 OrangeEngine 这一侧基于该 feature 写代码，必须拆成两个独立的 session：先在一个 session 把 OrangeRender 的改动落地、合并、tag/commit 固化；再在新的 session 里 bump `vendor/OrangeRender` 并消费。原因：同 session 双向操作会把"渲染器还没真正稳定的 API"当成既成事实写进引擎，下游一旦回退就会留下半成品；并且容易绕过 OrangeRender 自己的 review / wiki / 版本纪律。遇到"engine 这边发现 OrangeRender 缺东西"的情况，本 session 的正确动作是：停下当前 engine 任务，仅把需求记录下来（commit message / issue / 临时笔记），结束本 session，下次开新 session 再处理。
- **需求 / bug 提交入口**：发现 OrangeRender 缺能力 → 追加到 `vendor/OrangeRender/docs/incoming_feature.md`；发现 OrangeRender bug → 追加到 `vendor/OrangeRender/docs/incoming_bugs.md`。条目命名约定 `## FEATURE-<日期>-<slug>` / `## BUG-<日期>-<slug>`，便于后续 commit / PR title 引用。OrangeRender 维护者按其仓内 `CLAUDE.md` 的"外部需求 / Bug 处理工作流"评审 + 拆解 + 落地 + 归档，整个评审过程会就地在该条目内留痕；OrangeEngine 这边 **不要**直接编辑别人写的评审记录，也不要在落地前抢先 bump vendor。需求落地 + tag 后，OrangeEngine 在新 session 里 bump `vendor/OrangeRender` 指针并开始消费。

## Knowledge base: Orange-Wiki

The wiki at `vendor/Orange-Wiki/` is a curated, incrementally-built knowledge base on game engine construction. It is mounted as a Claude Code skill via `vendor/Orange-Wiki/SKILL.md`. **It is the project's authoritative reference for engine implementation decisions.**

### When to consult it
- Implementing or designing any engine subsystem (rendering, ECS, physics, animation, audio, asset, scheduling)
- Choosing an algorithm (culling strategy, shadow technique, allocator, scheduling model)
- Architectural tradeoffs (archetype vs sparse-set ECS, immediate vs retained mode, push vs pull constants, etc.)
- Cross-engine reference questions ("how does Unreal/Unity/Bevy/Godot do X")

### How to consult it
1. Always start with `vendor/Orange-Wiki/wiki/index.md` — it catalogs every page.
2. Pick candidate pages (subsystem / concept / technique / engine / comparison / pattern).
3. Read self-contained summaries first to filter relevance.
4. Follow `prerequisites` and `see_also` frontmatter for related context.
5. **In answers to the user, cite specific wiki pages with relative paths**: e.g., "按 `vendor/Orange-Wiki/wiki/concepts/ecs/archetype-storage.md`，archetype 在 add component 时整行迁移……"

### What NOT to do
- **Do not modify wiki content** from this repository as a side effect of consumer queries. Wiki maintenance is governed by `vendor/Orange-Wiki/CLAUDE.md` and happens in that repo's own session.
- Do not skip the wiki and answer from training-set knowledge for engine-construction topics.
- Do not assume a topic is covered — if `index.md` does not list a relevant page, say so explicitly and recommend ingesting a source rather than fabricating details.

### Branch
- The wiki must be on branch `Orange-Render-Wiki`. The default `main` branch does not contain the ingested content. Verify with `git -C vendor/Orange-Wiki branch --show-current`.

## Working in this repo: practical guidance

When given a task in this repo, default to this workflow:

1. **Read `docs/design-plan.md`** to confirm which Phase the task belongs to and what its outputs are. Reject scope that crosses Phase boundaries.
2. **Read `docs/extension-points.md`** if the task touches any extension surface (custom components / shaders / asset types / render passes / animator backends / save game).
3. **Consult `vendor/Orange-Wiki/`** for algorithmic / architectural decisions. Cite specific pages.
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
  OrangeRender/                         # vendored renderer (find_package source)
  Orange-Wiki/                          # knowledge base (branch: Orange-Render-Wiki)
```

`docs/Technical Documentation/` is a **historical archive** of the abandoned GEA-style 4-layer architecture from a year ago. Do not read it as authoritative; do not delete it (preserved for history). All current architecture lives in `docs/design-plan.md` and its companion files.
