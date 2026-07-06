# PIE 双语言玩法宿主：方案 + Roadmap M0-M10

> 决策记录 = [ADR-021](../../Orange-Wiki/case-studies/orange-engine/decisions/ADR-021-pie-gamemodule-dual-language-hosting.md)（accepted，用户 2026-07-06 拍板）。
> 本文档是执行计划（roadmap 写计划，ADR 写决策）；C# 侧细节设计另见 `docs/pie-csharp-scripting-design.md`（M5 消费）。
> 需求源头 = `docs/engine-known-gaps.md` 的 `GAP-2026-05-27-play-in-editor`；maturity-roadmap B1 各子项映射见文末对照表。

## 0. 目标一句话

让 OG 首游（纯 C++ 史莱姆）在 OrangeEditor 里 **Play / 调参 / Stop 还原**，同时 C# 脚本走同一套宿主抽象——`IGameModule` 统一生命周期接口 + 双宿主实现，发布侧零编辑器污染。

## 1. 三仓实况盘点（2026-07-06 探查，方案的事实基础）

### 已有、不用重造

| 地基 | 证据 |
|---|---|
| Play 状态机全量存在 | `PlayState{Edit,Play,Paused}`（`context/EditorSceneContext.h:46-62`）+ 帧末 `ApplyPendingPlayOp`（`EditorRenderLayer.cpp:1718-1985`）：EnterPlay = 快照落盘（带 pid 唯一名）→ 装配 PhysicsWorld（遍历 RigidBody+Collider 注册 body）→ Audio 实例化 → VfxSystem；Stop = 拆卸 → `Scene::Load` 还原 → 清 undo 栈 |
| Play/Edit 双 tick 路径 | Play 态全量 simulation tick（physics/vfx/`TickAnimators`/audio，`EditorRenderLayer.cpp:219-296`）；Edit 态仅 B2.6 单 animator 预览 tick（`:298-326`）；Play 期编辑已门控 |
| 快照身份稳定 | scene/world 1.19 + EntityGuid 双键（ADR-018）；`tests/scene/PlaySnapshotGuidTest.cpp` 锁住反复进出 Play 不漂移；内存快照 API 现成（`SaveSubtreeToString`/`LoadFromString`，`SceneSerialization.h:217/224`）但编辑器当前走落盘 |
| 游戏侧序列化钩子 | `SaveOptions/LoadOptions.extraSerializers` + `EditorHost::extraSerializers` —— 自定义组件序列化的注册机制现成 |
| 渲染逃生舱口 | `Pipeline::InsertPass(PipelineStage, unique_ptr<IRenderPass>)`（`Pipeline.h:259`；`IRenderPass.h:43-79`，AfterShadow/AfterMainPass/AfterPostProcess 三档）——OG SlimeMetaballPass 已消费 |
| C# 宿主全链 headless 绿 | ScriptHost（nethost/hostfxr，PIMPL 零 CLR 泄漏）→ ScriptRuntime（7 个 `[UnmanagedCallersOnly]` 入口 + 函数指针表绑定）→ ScriptComponent/ScriptSystem（StartWorld/Tick/StopWorld）；`script_system_test` 端到端 |
| Toolbar/OE-MCP Play 同路径 | `ToolbarPanel.cpp:145-189` 与 `McpCommandHandler.cpp:2029-2069` 都写 `pendingPlayOp`，走同一 `ApplyPendingPlayOp` |

### 缺口（本 roadmap 要补的）

1. ~~🔴 **离屏路径不执行 InsertPass**（唯一硬技术阻塞）：`Pipeline.h:131-135` S1 契约明示离屏只跑主 pass、InsertPass silent-ignore；三处 `IRenderPass::Execute`（`Pipeline.cpp:3259/3350/3647`）全在 window-mode 分支。史莱姆 SDF 进编辑器视口会消失。对比：bloom 已接离屏（`Pipeline.cpp:1966`）。~~ → **✅ M1 已修**（AfterShadow + AfterMainPass 两档接入离屏；AfterPostProcess 因绑 swap-chain 保持 window-only）。
2. **无游戏模块概念**：EnterPlay 只装配引擎内置系统；无自定义 system/schema/pass 的模块注册入口；引擎无统一 SystemScheduler（`ISystem.h:14-17` 明示暂缺，各子系统 tick 由消费者手动串——本 roadmap 沿用该模式，不引入调度器）。
3. **编辑器 exe 形态**：74 个 .cpp 直编进 exe（`tools/OrangeEditor/CMakeLists.txt:102-195`）+ 约 1100 行巨型 main；无 lib、不 install。路径依赖 = `ChdirToRepoRoot()` cwd 副作用（`main.cpp:167-194`）+ 5 处相对路径（codicon `main.cpp:662` / `editor_settings.json:776` / imgui.ini 无 `SetIniFilename` / 启动场景 / editor shader）。
4. **C# 侧编辑器断线**：`ORANGE_ENGINE_WITH_DOTNET` 默认 OFF 且编辑器 CMake 不感知；EnterPlay 无脚本 S 步；实况 = net8.0 + Default ALC（ADR-017 的可卸载 ALC 被 de-scope，M8 兑现）；SDK 导出面无 nethost 再分发/`OrangeScriptSDK.dll` install（M10 补）。
5. **OG spike-01 无模块边界**：引擎装配全在 `main()` 栈上（`main.cpp:1840-1994`）、玩法状态全在 `SpikeLayer` 私有成员、`AppHost::Run()` 独占主循环与窗口；blob/droplet 非 ECS 组件（编辑器无从 authoring）；好消息 = `Blob.h` 纯 header 零引擎依赖值语义，`OnEnterPlay=Reset / Tick=FixedStep / OnExitPlay` 映射干净。
6. **全程不涉 OrangeRender**——以上全部在 OE（+OG）层。

## 2. 架构

```
                    ┌────────────────────────────────────┐
                    │   IGameModule（引擎层，M2 定稿）     │
                    │ 注册期: RegisterSchemas /            │
                    │   RegisterSerializers /              │
                    │   RegisterRenderPasses               │
                    │ Play 期: OnEnterPlay / Tick /        │
                    │   OnEvent / OnExitPlay               │
                    └────────┬──────────────┬─────────────┘
              实现方                        │ 宿主（消费方）
        ┌────────────────┐        ┌────────┴─────────┐
        │ SlimeGameModule │        │ orange_editor lib │←─ 挂进既有 ApplyPendingPlayOp
        │  (OG, C++, M4)  │        │ + 瘦 EditorApp    │   + Play tick + 输入/相机路由
        ├────────────────┤        │  (M3)             │
        │ ScriptSystem    │        ├──────────────────┤
        │ 包装 (OE,C#,M5) │        │ orange_runtime    │←─ 发布瘦宿主（M10 正式化；
        └────────────────┘        │ 瘦 main (M4 起雏形)│   与编辑器装配逻辑收敛同源）
                                  └──────────────────┘

  OG 同一份游戏代码编进两个 target：
    SlimeEditor.exe = orange_editor + orange_engine + SlimeGameModule   ← 开发
    Slime.exe       = 瘦 runtime    + orange_engine + SlimeGameModule   ← 发布，不链 editor lib
```

**接口草案**（M2 定稿时按此细化，签名允许调整）：

```cpp
// include/orange/engine/game/IGameModule.h —— 编辑器宿主与发布 runtime 宿主共用
struct GameModuleContext
{
    World* pWorld;                    // 宿主拥有，模块只借用
    Phys::PhysicsWorld* pPhysics;     // Play 期宿主装配；模块可在 OnEnterPlay 加自己的 body
    AssetRegistry* pAssets;
    Render::Pipeline* pPipeline;
};

class IGameModule
{
public:
    virtual ~IGameModule() = default;
    virtual const char* Name() const = 0;
    // 注册期：宿主启动即调一次，Edit 态就生效（组件可 authoring；pass 常驻、无数据早退）
    virtual void RegisterSchemas(/* schema registry */) {}
    virtual void RegisterSerializers(/* extraSerializers 挂点 */) {}
    virtual void RegisterRenderPasses(Render::Pipeline&) {}
    // Play 生命周期：挂进 ApplyPendingPlayOp 的 S 步序列 / runtime 宿主启动序列
    virtual void OnEnterPlay(GameModuleContext&) {}
    virtual bool WantsOwnPhysicsStep() const { return false; }  // 开放问题①的暂定形态
    virtual void Tick(GameModuleContext&, float dt) {}          // 模块自管固定步长（spike-01 现状）
    virtual void OnEvent(const Platform::WindowEvent&) {}       // 视口聚焦时宿主路由
    virtual void OnExitPlay(GameModuleContext&) {}
};
```

**关键取舍**（论证见 ADR-021，此处只记结论）：

- C++ 宿主 = per-game editor 静态链入起步（STATIC 引擎拓扑下零 ABI 风险），DLL 热加载留 M7；
- C# = 同一接口第二实现，防接口偏科；C# 项目始终可用原版共享 OrangeEditor；
- 接口放引擎层（发布 runtime 也要用）；
- workspace 降级为 `EditorAppConfig` 参数注入，完整项目模型留 M6；
- 快照沿用落盘，内存快照留 M9；
- 不引入 SystemScheduler，沿用手动串 tick。

## 3. Roadmap

依赖链：`M1 ∥ M2 → M3 → M4 → M5`；M4 之后四条独立支线 `M6→M7`、`M8`、`M9`、`M10`。
每阶段一个 session 起步（M3 预计 2-3 个），per-session 单子仓纪律全程适用；M3→M4 之间 umbrella bump。

### M0 · 决策与计划落盘（Wiki+OE 纯文档，S）✅ 2026-07-06

ADR-021 + 本文档 + `engine-known-gaps.md` PIE GAP 拍板更新 + maturity-roadmap B1 修订。

### M1 · 离屏路径接通 InsertPass（OE，S）✅ 2026-07-06

- **交付（实际）**：`RenderOffscreen` 接 **AfterShadow + AfterMainPass 两档** InsertPass hook（`src/render/Pipeline.cpp`，镜像 window 路径同款 ctx 构造）；更新 `Pipeline.h` 离屏能力注释（顺带修正它已过时的"只跑主 pass"——bloom/godrays/SSAO 等实际早已接入离屏）。
  - **三档→两档的实况修正**：`AfterPostProcess` 语义绑 stage-B swap-chain（pass 走 `renderer.SubmitItem`，见 `IRenderPass.h:25`），而 `RenderOffscreen` 无 swap-chain 阶段（passthrough 直写 viewportColor）——**无离屏等价物，保持 window-only**。SlimeMetaballPass 用的是 `AfterMainPass`，不受影响。
  - **layout parity 论证**：两个 hook 的前置 GPU 状态与 window 路径**逐调用一致**（`RecordShadowPass/Spot/Point` → hook；`RecordOffscreenPass → DrawParticles` → hook），故结构镜像即保证 layout 一致，window 能跑的 pass 在离屏必能跑（含 y-flip：同一 `viewProj`）。
- **验收（已过）**：`pipeline_offscreen_test` 加 CountingPass 断言，真 Vulkan（RTX 5070 Ti）跑出 `AfterShadow=1 AfterMainPass=1 AfterPostProcess=0`；render 相关 ctest 25/25 全绿（golden PBR/shadow/thumbnail/bloom 零回归）；invariant lint 干净。
- **提交**：见本 session commit（OE 代码 + 本文档同 repo）。

### M2 · IGameModule 接口 + Play 生命周期挂接（OE，M）

**M2.1 引擎接口 + 宿主核心 ✅ 2026-07-06**（headless，不碰编辑器）：

1. `include/orange/engine/game/IGameModule.h` —— `IGameModule` 接口 + `GameModuleContext`。**对 §2 草案的实况修正**：删掉 `RegisterSchemas`（schema 是编辑器 Inspector 侧概念，引擎 runtime 无 schema registry；游戏组件 Inspector 注册留编辑器层 M3/M5）；保留引擎级面 = `RegisterRenderPasses(Pipeline&)` + `ComponentSerializers()`（返回 `std::span<const Scene::ComponentSerializerEntry>`，宿主收集填进 `Scene::Save/LoadOptions.extraSerializers`）+ 生命周期 `OnEnterPlay/Tick/OnEvent/OnExitPlay` + `WantsOwnPhysicsStep`（开放问题①）。
2. `include/orange/engine/game/GameModuleHost.h`（header-only）+ `game.h` 聚合 —— 可复用扇出驱动：注册期 `RegisterRenderPasses`/`CollectSerializers`/`AnyWantsOwnPhysicsStep`；Play 生命周期 `EnterPlay`(正序)/`Tick`/`OnEvent`/`ExitPlay`(逆序)，带 play-state 护栏（未 EnterPlay 的 Tick/OnEvent/ExitPlay no-op，重复 EnterPlay no-op）。编辑器宿主与发布 runtime 宿主共用。
3. 验收：`game_module_host_test`（headless，Pipeline 默认构造测 InsertPass 扇出）3/3；lint（含 game.h aggregator-completeness）+ drift 干净。

**M2.2 编辑器接线 ✅ 2026-07-06**（属编辑器代码，与 M3 lib 化同期）：

1. ✅ `EditorHost` 挂 `GameModuleHost gameModules`（值成员，沿 plugin 注册表先例，不塞 mega-class）；main 启动装配段留**模块注册站点**（原版 OrangeEditor 无模块 → gameModules 恒空 → 护栏令全部扇出 no-op、零行为变化；per-game editor M4 起在此 `AddModule`）。
2. ✅ 注册期扇出：`CollectSerializers` merge 进 `editorHost.extraSerializers`（启动场景 Load 之前）；`RegisterRenderPasses` 因 viewport Pipeline 是 ScenePanel 首帧 lazy 创建，挂在 `EnsureScenePipeline` 创建块（Edit 态常驻，pass 无数据自早退）。
3. ✅ `ApplyPendingPlayOp`：EnterPlay 末（S2 快照/S3 physics/audio/S4 vfx 就绪后）调 `host.EnterPlay(ctx)` = S5；Stop 首（拆卸前，逆序对偶）调 `host.ExitPlay(ctx)`。ctx = `{pWorld, pPhysics(mpPhysicsWorld), pAssets, pPipeline(mpScenePipeline)}`。
4. ✅ Play tick：`host.Tick(ctx,dt)` **先于**宿主 physics step；`AnyWantsOwnPhysicsStep()` 时宿主让位（gate 掉 `mpPhysicsWorld->Step`+写回，开放问题①落地）。
5. ✅ 输入路由：`OnEvent` Play-gated 转发 `WindowEvent` → `host.OnEvent`（不消费，编辑器并行收）。
- **本阶段验收（已过）**：编辑器**编译 + 链接绿**（EditorHost.h 触发多 TU 重编）、shader/资产齐全、gameModules 空态零回归；invariant lint 干净。
- **defer 到 M4**（随真实 SlimeGameModule + 游戏相机落地，dogfood-gated）：① Enter/Stop 生命周期 + module tick 的行为 dogfood（当前无模块可看）；② 输入路由细粒度 gate「仅 Scene 视口聚焦才路由」（避免 Inspector 输入漏进游戏，当前无 focus 信号）；③ **Play 期切 World 内游戏相机 / Edit 期编辑器相机**（当前无游戏相机概念可切，硬接半版本会回归编辑器现行稳定轨道相机，故 defer——耦合 camera-editor-vs-runtime GAP）。
- **纪律**：注册走 extraSerializers 既有机制，禁止 mega-class 加游戏分支（ADR-001）——已遵守（gameModules 挂 host、无 mega-class 游戏分支）。

### M3 · editor lib 化 + SDK 导出（OE，**L**，预计拆 2-3 session）

**M3 step1 · 机械 lib 拆分 ✅ 2026-07-06（build + 运行时 + 视觉三重验证，GUI dogfood gate 已关）**：
- `orange_editor` STATIC lib target 建成，承载全部编辑器子系统 + 应用装配；`OrangeEditor.exe` 退化为**瘦 main**（3 行，仅 `return Orange::Editor::RunEditorApp(argc, argv);`）。
- 原 ~1114 行 `main.cpp` 全量下沉为 lib TU `EditorApp.cpp` 的 `Orange::Editor::RunEditorApp`（含 headless import CLI 分支 + 启动装配 + 主循环 + 关停），**逐字节不变**——ChdirToRepoRoot / codicon / editor_settings / imgui.ini / 启动场景 / editor shader 5 处路径逻辑一律未动，故运行时行为应与改前一致（step2 才参数化）。`.rc` 资源留 exe，vendor C 源（mikktspace/OpenFBX）+ /W0 豁免随 .cpp 进 lib。
- link：引擎/imgui/vulkan/glfw 由 PRIVATE 升 **PUBLIC**（STATIC lib 不嵌依赖对象码，exe/per-game editor 经链 orange_editor 传递性解析符号）。
- **验证**：in-tree 全量构建绿（`orange_editor.lib` + `OrangeEditor.exe` 均产出、0 error、lint 干净）；headless import CLI 端到端跑通（`main→RunEditorApp→ChdirToRepoRoot→argv 解析→usage→exit2`，证 exe→lib 入口链）；tests 直接编译单个编辑器 .cpp 源不受影响、root VS_STARTUP_PROJECT/add_subdirectory 仍指 exe。
- **GUI 真机 dogfood ✅**：启动编辑器 → 日志确认 `loaded editor_settings.json` + `world entities=20`（SeedDemoWorld 兜底，demo.scene.json 本地被删）+ `ImGui dock + multi-viewport ready`，无 crash/assert；截图视觉确认 **5 处 cwd 路径全对**——codicon 图标（工具栏 ▶⏸⏹+勾选框）/ imgui.ini 布局 / editor_settings / 启动场景（3D 视口渲染 20 实体含 PBR+bloom）/ editor shader（grid 渲染），中文字体也正常。**GUI 零回归证实**。

**M3 step2 · 参数化 + 导出（剩余，dogfood-gated，建议独立 session）**：
  1. `EditorAppConfig`：projectRoot / configDir / assetRoot / IGameModule 列表显式注入（`RunEditorApp` 改签名吃 config）；`ChdirToRepoRoot()` 降级为"无配置时的默认值推导"，消灭 cwd 全局副作用；imgui.ini 显式 `SetIniFilename`；main 里 ~230 行 RenderDevice/Renderer/ImGui 初始化已随 EditorApp.cpp 进 lib（step1 完成），step2 只做参数化；
  2. install/export：lib + 对外头布局（编辑器头当前平铺在 `tools/OrangeEditor/`，需梳理公共面）+ editor shaders（`shaders/orange_editor/*.spv` 现仅 build-tree，仿 `orange_engine_copy_builtin_shaders` 补 install + 拷贝 helper）+ codicon/主题资产；importer 私有 vendor（tinyobjloader/cgltf/OpenFBX）在 STATIC 导出下的依赖解析。
- **验收（step2）**：原编辑器全功能零回归（重点回归清单：codicon 字体 / imgui.ini 布局 / editor_settings / 启动场景 / editor shader，全部有 cwd 前科）；OG 侧 `find_package` 链 editor lib 的 install smoke 测试（仿 `tests/install/editor_build_smoke.cmake`）。
- **scope 红线**：**不**拆 AudioEngine/ThumbnailService 做 headless lib——editor lib 第一版允许依赖 Vulkan/ImGui，headless 测试 seam 维持 `EditorAssetContext` 现状。

### M4 · 史莱姆进编辑器（umbrella bump → OG，M）

- **交付**：
  1. spike-01 抽 `SlimeGameModule`：引擎装配移交宿主，玩法状态从 `SpikeLayer` 成员迁入 module；`RegisterRenderPasses` 注册 SlimeMetaballPass；输入沿 `In::ActionMap`，事件改经 `OnEvent`；
  2. **手感旋钮组件化**：BlobParams/手感常量做成 schema 注册的 ECS 组件 → Inspector 实时调参（PIE 对史莱姆的最大即时价值，正中 2026-07-05 dogfood 手感迭代痛点）；
  3. `SlimeEditor.exe`（瘦 main：EditorAppConfig 注入 module）+ `Slime.exe`（瘦 runtime main，保持 window 模式可独立跑）双 target；顺手删 builtin shader 的 build-tree 拷贝 workaround（install GAP 已修）。
- **验收**：编辑器 Play → 史莱姆可操控、SDF 在离屏视口显示（M1 成果的真实消费）、Stop 还原；Inspector 拖手感参数不重编生效；`Slime.exe` 行为与改造前 spike-01 无回归。
- **说明**：第一版关卡仍是模块代码生成（spike-01 无 .scene.json），视口内关卡盒走 debug-draw（离屏已支持）；关卡 ECS/tilemap 化是独立后续，不阻塞本 epic。

### M5 · C# 第二宿主接线（OE，M）

- **交付**：ScriptSystem 包成内置 IGameModule 实现（或等价接线，按 `docs/pie-csharp-scripting-design.md` 的 EnterPlay S4 设计）挂进同一生命周期；编辑器构建 `ORANGE_ENGINE_WITH_DOTNET=ON` 接通（含 CMake 感知）；ScriptComponent Add-Component + Inspector tweakable（C# `System.Reflection` 反射 public field 喂 schema，引擎 C++ 侧仍零反射库）。
- **验收**：C# Mover 脚本在编辑器 Play 里驱动实体、Stop 还原（maturity-roadmap B1 dogfood 项闭环）；接口无 C++ 特化泄漏（两实现并存即验证）。

### M6 · workspace / 项目模型（OE，M，ADR-022）

- **交付**：
  1. `.orangeproject` 项目文件（JSON，schema 版本化）：项目名 / asset 根（可多）/ 启动场景 / 游戏模块引用（`"static"` = per-game editor 内建；DLL 路径 = M7 预留；C# assembly 列表）/ 渲染设置；`EditorAppConfig` 从项目文件填充；
  2. AssetRegistry 多挂载根：`engine://` + `project://`（⚠️ 唯一待探点：AssetRegistry 现状是否单根假设）；
  3. 场景资产引用根相对化：scene schema 升版（1.19→1.20）+ 向后兼容（老路径按 `project://` 解释）；
  4. per-project 编辑器状态（imgui.ini / editor_settings 随项目走）；
  5. File→Open Project + 最近项目 + `--project` 参数直开。
- **验收**：引擎 demo 包成 `.orangeproject` 后任意 cwd 打开零回归；OG 建 `slime.orangeproject`，SlimeEditor 经它解析资产、删 POST_BUILD 拷贝 workaround。

### M7 · DLL 游戏模块宿主（OE+OG，**XL**，ADR-023 先行）

共享 `OrangeEditor.exe` 运行时 `LoadLibrary(game.dll)`——"一个编辑器切多项目" + 改 C++ 不重启编辑器。接口不变，纯换宿主实现。

- **前置决策（ADR-023）**：引擎 SHARED 化（推荐：一次付清，`orange_engine.dll` 单份全局状态；成本 = MSVC 导出标注或 `WINDOWS_EXPORT_ALL_SYMBOLS`〔64K 符号上限风险〕）vs 双静态 + 严格边界纪律（改动小但纪律成本永久化：ENTT_API 共享 type context、跨界分配、RHI 对象传递逐点审）。**等 M4 用出真实痛点、有实据再拍。**
- **交付**：ADR-023 + （若 SHARED）引擎 SHARED 化全量回归；DLL 宿主机制（shadow copy DLL+PDB 绕 MSVC 锁 → `extern "C"` 单入口 `OrangeCreateGameModule` → 注册期调用）；**session 级热重载**：Stop → 注销 pass/schema/serializer（注销机制是新工作，现状只有 `RemovePassesAt`）→ FreeLibrary → 重编 → LoadLibrary → 重注册 → EnterPlay，场景状态从 PIE 快照天然还原；DLL file watcher + 状态栏"模块已过期"提示；OG 的 `slime.orangeproject` 模块引用切 DLL，SlimeEditor target 降级备用。
- **验收**：共享 OrangeEditor 开 slime 项目 → Play → Stop → 改一行手感代码重编 DLL → 编辑器不重启重载再 Play 生效。
- **明确不做**：Play 中途活状态热替换（Live++ 级），性价比不成立。

### M8 · C# collectible ALC 热重载（OE，M，依赖 M5，ADR-017 amend）

兑现 ADR-017 de-scope 的"可卸载"，C# 拿到比 C++（session 级）更快的迭代——改脚本不 Stop。

- **交付**：ALC 分层（`OrangeScriptSDK.dll` 留 Default ALC 永不卸载——C++ 侧缓存的 7 个 `[UnmanagedCallersOnly]` 入口指针因此恒有效；游戏程序集进 collectible ALC）；卸载协议（StopWorld 释放全部 GCHandle → 断 delegate 引用 → `Unload()` + 弱引用轮询确认 → 超时报泄漏诊断）；状态保持重载（卸载前序列化 public field〔fieldOverrides 机制现成〕→ 重载 → 重实例化 → 回灌）；程序集 file watcher（Edit 态自动重载；Play 态脚本暂停→重载→恢复）；ADR-017 补记。
- **验收**：Play 中改 `Mover.cs` 重编 → 行为更新、字段值保持、无 ALC 泄漏。

### M9 · PIE 体验打磨（OE，M，M4 后可拆件插队）

全部小件、彼此独立，适合当 filler：

1. **内存快照**：EnterPlay 落盘 `Save(path)` 换 `SaveSubtreeToString`、Stop 用 `LoadFromString`（API 现成）；`PlaySnapshotGuidTest` 改跑内存路径；
2. **帧步进**：Paused 态 Step 按钮（tick 恰一个固定步长，物理手感游戏调试刚需）+ 时间缩放（0.1×/0.5×/1×）；
3. **Game/Scene 双视口**：Game 面板（游戏相机）与 Scene 面板（编辑器相机自由飞，Play 期可旁观）并存；离屏 Pipeline 多实例有 ThumbnailService 先例，成本在面板与输入路由归属；
4. **Play 期调参回写**：放开 Inspector 数值字段（Play 期即时生效、Stop 丢弃）+ "Copy current values" 手动带回 Edit 态——史莱姆手感调参完全体；
5. **OE-MCP 对齐**：补 `step()` / 时间缩放 / Play 期读 gameplay 状态 tool，让自动化 dogfood 驱动完整 PIE 回路。

### M10 · 打包发布管线（OE+OG，M→L，三级渐进，ship 哪级用哪级）

- **级别 1 · install target（M，ship 最低要求）**：
  1. `orange_runtime` 瘦宿主库正式化（`RuntimeHost::Run(IGameModule&, RuntimeConfig)`：窗口/主循环/加载启动场景/装配 Physics·Audio·Vfx·脚本——**与编辑器 `ApplyPendingPlayOp` 装配逻辑收敛同源**，消灭"编辑器能跑、发布行为不同"漂移面）；
  2. 游戏侧 `cmake --install`：exe + 引擎 builtin shaders + 游戏 shaders + assets 过滤拷贝（排除 .blend 等源文件）；
  3. C# 运行时再分发（仅当游戏用 C#）：补 `OrangeEngineConfig` 缺失的 dotnet 导出面——nethost.dll 拷贝 helper + `OrangeScriptSDK.dll` + runtimeconfig + self-contained .NET runtime（约 +40MB）；纯 C++ 游戏零负担。
- **级别 2 · 资产 cook（M，文件量痛点触发）**：pak 单档案 + 索引（AssetRegistry 加 pak mount 后端，`project://` 语义不变——M6 挂载根设计预留的接缝）；纹理离线 BCn；scene JSON→二进制可选。
- **级别 3 · 分发质感（S，首次对外发布前）**：exe 图标/版本信息、崩溃 minidump + 日志落 `%APPDATA%`、用户设置与存档路径规范化。
- **验收（级别 1）**：install 目录拷到无 Vulkan SDK / 无 .NET SDK 干净机器双击能玩。

## 4. 风险清单

| 风险 | 缓解 |
|---|---|
| M1 离屏 RT 与 window 路径的 layout/y-flip/色彩空间差异 | 最小测试 pass 先验通；史莱姆 y-flip 有前科经验 |
| M3 路径回归（cwd 标记→codicon 字体崩溃有前科） | 全部经 `EditorAppConfig` 显式注入、默认值保持现行为；回归清单点名 5 处相对路径 |
| M3 scope 膨胀（headless lib 化诱惑） | 红线写死：第一版 editor lib 允许依赖 Vulkan/ImGui |
| 物理 step 所有权冲突（宿主 step vs 模块自管 accumulator） | M2 以 `WantsOwnPhysicsStep` 类声明位裁定（开放问题①） |
| 编辑器视口键盘焦点/ImGui 捕获影响手感 | M2 输入路由设计点名处理；M4 真机 dogfood 验收 |
| M7 引擎 SHARED 化（全 roadmap 最重单件） | 推迟到 M4 出实据后 ADR-023 拍板；64K 符号上限提前测 |
| M3 期间主干功能开发被阻塞 | M1/M2 先行不动编辑器结构；lib 化按"先机械搬迁后参数化"两步走 |

## 5. 开放问题（各阶段裁定，不阻塞 M0）

1. **物理 step 所有权**（M2）：`WantsOwnPhysicsStep()` 旗标 vs 宿主固定步长回调（`FixedTick(dt)`）——倾向前者（改动小、spike-01 直迁）。
2. **模块 Tick 在 Play tick 序列中的位置**（M2）：暂定 module Tick → 宿主 physics step（若未接管）→ vfx → anim → audio。
3. **AssetRegistry 是否单根假设**（M6 前探明，不影响 M0-M5）。
4. **编辑器头公共面梳理粒度**（M3）：全量 install vs 挑选宿主必需子集——倾向后者起步。
5. **`OnEvent` 事件形态**（M2）：透传 `Platform::WindowEvent` vs 抽象输入快照——倾向透传起步（spike-01 现状即事件喂 `InputContext`）。

## 6. maturity-roadmap B1 对照

| B1 子项 | 本 roadmap 落点 |
|---|---|
| B1.0 形态拍板（2026-06-02 脚本/C# 单轨） | **ADR-021 修订为双宿主**；ADR-017 收编为第二实现 |
| B1.1 workspace（原"硬前置"） | **降级** M3 参数注入起步；完整项目模型 = M6 |
| B1.2 游戏侧 system/component 被编辑器发现 | M2（注册期接口）+ M4（首个真消费者） |
| B1.3 Play/Pause/Stop 状态机 | 已全量存在（实况修正）；M2 只做挂接 |
| B1.4 输入/相机 edit↔play 切换 | M2 |
| B1.4' 完整热重载（collectible ALC） | M8 |
| B1.5 运行时落地 | M2-M5（双宿主）+ M10（发布 runtime） |
