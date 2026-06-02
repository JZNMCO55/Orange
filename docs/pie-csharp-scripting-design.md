# Phase B1 设计提案：Play-in-Editor + C# 脚本运行时

> 状态：**设计提案（未实现）**。用户 2026-06-02 拍板「PIE 重点 + 脚本语言用 C#」后的地基调研。
> 本文是**提案不是决策**——落地前应升 ADR 由用户裁定关键选型（运行时 host、binding 方式）。
> 参考：Wiki [`concepts/gameplay/scripting-system.md`](../../Orange-Wiki/wiki/concepts/gameplay/scripting-system.md)
> （六种架构模式 + 选型 + 热重载陷阱）；Unity 的 C# via Mono/IL2CPP 是 C# 脚本引擎的范本。

## 1. 目标与范围

让游戏逻辑（玩法、AI、触发器、状态机）用 **C#** 写、在编辑器里 **Play** 即时运行、改脚本
**热重载**——对标 Unity MonoBehaviour 的开发循环。按 Wiki 六模式属 **#3~#4**（脚本扩展/
定义 GO 行为 + scripted component），不是 #5/#6（引擎本体不交给脚本）。

**非目标**（与 umbrella 范围一致）：脚本写引擎子系统、可视化脚本（Blueprints 路线）、
脚本驱动渲染。性能热点（批量更新/物理/渲染）留 native C++。

## 2. 已有地基（必须复用，别重造）

编辑器**已有完整 Play Mode 状态机 + 快照回滚**——PIE 脚本是往这套机制里加一层，不是从零做：

| 已有件 | 位置 | 复用方式 |
|---|---|---|
| `PlayState`（Edit/Play/Paused）状态机 | `tools/OrangeEditor/context/EditorSceneContext.h:46` | 脚本生命周期门控同款（gizmo/编辑命令已按它 gate） |
| `PlayOp::EnterPlay`：World 快照落盘 + 启动 simulation | `EditorRenderLayer.cpp:1488`（S2 Save snapshot / S3 PhysicsWorld） | Stop 从 `playSnapshotPath` 还原 → **脚本对 ECS 的改动天然被回滚**，无需脚本侧做 undo |
| Play 每帧 tick（physics Step + 写回 ECS） | `EditorRenderLayer.cpp:223` | 脚本 `OnUpdate` 挂在同一 Play tick，物理后 |
| 引擎动画 tick 入口 `AnimationSystem` | `src/animation/`（ProceduralAnimator / AnimationStateMachine runtime 齐全） | 脚本可驱动 animator 参数 |

集成点（最小侵入）：
- **EnterPlay**（`EditorRenderLayer.cpp` S3 之后加 **S4**）：初始化 C# runtime + 为带
  `ScriptComponent` 的 entity 实例化 C# 对象 + 调 `OnStart()`。
- **Play tick**（physics Step 之后）：对所有活脚本实例调 `OnUpdate(dt)`。
- **Stop / ExitPlay**：调 `OnDestroy()` + 卸载 runtime；ECS 改动由现有快照还原回退。

## 3. C# 运行时 host 选型（关键决策，待 ADR）

C# 要在 C++ 进程里跑，需嵌入一个 CLR。三条路线：

| 方案 | 机制 | 优 | 劣 |
|---|---|---|---|
| **A. .NET hosting (CoreCLR / `hostfxr`/`nethost`)** ⭐推荐 | 官方 `nethost` C API 加载 .NET 运行时 + `load_assembly_and_get_function_pointer` 拿托管函数指针 | 现代 .NET（8/9）；官方支持；性能好；NuGet 生态；`UnmanagedCallersOnly` 零封送回调 | 运行时随程序分发体积较大；domain 隔离弱（卸载靠 `AssemblyLoadContext`） |
| **B. Mono embedding** | 嵌入 Mono VM（Unity 早期同款）`mono_jit_init` + `mono_runtime_invoke` | 成熟（Unity 验证多年）；AppDomain 支持热重载干净；体积可裁剪 | 老 Mono；API 偏底层；新 .NET 特性滞后 |
| **C. IL2CPP / AOT** | C# → IL → C++ AOT 编译 | 无 GC pause 顾虑、可裁剪、发布期性能 | **无热重载**——与"编辑器即时迭代"目标冲突；只适合 ship build |

**推荐**：开发期 **A（CoreCLR + `AssemblyLoadContext` 可卸载上下文）** 走热重载；未来 ship build 可选 C（AOT）做发布优化。先做 A，把 binding 层抽象到不依赖 host 细节，保留切换余地。

模块归属（按引擎 header isolation 纪律）：CLR host 是新的私有依赖，应隔离到
`src/script/dotnet/**`（类比 `src/physics/box2d/**`），公共头 `include/orange/engine/script/`
**不**暴露任何 CLR/hostfxr 类型——与 Box2D/DragonBones/miniaudio 同款单一消费区约束。

## 4. C++ ↔ C# binding 层

脚本要能操作引擎对象（entity / transform / 输入 / 物理 / 生成实体）。两个方向：

- **C# → C++（脚本调引擎 API）**：C++ 导出一组 `extern "C"` + `UnmanagedCallersOnly`
  函数（`Entity_GetPosition` / `Entity_SetPosition` / `World_FindByName` / `Input_GetAxis`
  / `World_Instantiate` …），C# 侧用 `[DllImport]` 或函数指针表 P/Invoke。Entity 句柄按
  `uint64`（EnTT entity + generation，或 EntityGuid〔ADR-013〕）
  跨界传，**不传裸指针**（GC 移动 + 生命周期风险）。
- **C++ → C#（引擎调脚本生命周期）**：拿托管 `OnStart/OnUpdate/OnDestroy` 的函数指针，
  Play tick 直接调。

封送原则：值类型（vec2/vec3/float/int）按 blittable struct 直传零拷贝；string 用
UTF-8 marshaling；集合走句柄 + 逐项访问 API（不整块封送）。**绑定面要薄**——Wiki 陷阱
「过度脚本化」：热路径别来回穿界，批量逻辑留 native。

## 5. 脚本组件模型（schema-first，遵守编辑器纪律）

对标 Unity MonoBehaviour：

```csharp
// 游戏侧 C#（OrangeGames 仓），用户写的玩法脚本
public class SlimePatrol : OrangeScript      // 基类由引擎 C# SDK 提供
{
    public float speed = 2.0f;               // [可在 Inspector 调的 tweakable]
    public override void OnStart() { ... }
    public override void OnUpdate(float dt) { Entity.Position += ...; }
}
```

- 引擎侧加 `ScriptComponent { assemblyPath, className, fieldOverrides{} }`（schema 化，
  序列化进 .scene.json；schema_version 出厂冻结，遵守 serialization 纪律）。
- 编辑器 Inspector 通过 **schema 注册**（**禁止** hardcode，遵守 OrangeEditor schema-first
  纪律）展示脚本的 public field（tweakable）——需要从 C# assembly 反射出 field 列表喂给
  schema（编译期不可知 → 运行时从 metadata 读，这是 script 组件 schema 的特例，需在 ADR
  说明它如何不破「无运行时反射库」引擎纪律：反射发生在 **C# 侧 / 编辑器工具侧**，不在引擎
  runtime C++ 里）。
- Add-Component 菜单走 plugin 注册项（同纪律），不改 mega-class。

## 6. 热重载

Wiki 核心价值主张。CoreCLR 方案：把游戏 assembly 加载进**可卸载** `AssemblyLoadContext`；
file watcher（编辑器已有 DCC import 的 file watch 思路可借）侦测 `.dll` 变化 → 卸载旧
ALC → 加载新 assembly → 重新实例化脚本对象。

**状态迁移**（Wiki 陷阱「热重载的状态问题」）：MVP 走**重置到初始状态**（重载即重跑
OnStart，活对象脚本状态丢弃）——最简单且对编辑器迭代够用；序列化/恢复运行时状态留后续。

## 7. 陷阱（直接引 Wiki scripting-system §陷阱）

- **热重载状态失效** → MVP 重置策略（见 §6）。
- **调试工具缺失** → 必须给脚本 stack trace + `print`/log 桥接 + 错误不崩编辑器（脚本异常
  catch 在 binding 边界，标红到 Console 面板，不传播进 C++）。
- **过度脚本化** → 热点留 native（§4 绑定面薄）。
- **GC 与帧时间** → CoreCLR GC 可能造成帧 spike；脚本侧避免每帧大量分配；必要时 server GC /
  对象池（属游戏侧规约，引擎文档提醒）。

## 8. 分期实施建议（每步可独立验证）

1. **B1.0 CLR host spike**：`src/script/dotnet/` 起 host，加载一个 hello-world assembly 调一个
   托管函数返回值——证明 CoreCLR 嵌入通路（headless 可测：host→invoke→assert 返回）。
2. **B1.1 binding 最小集**：Entity get/set position + log；C# `OrangeScript` 基类 SDK。
3. **B1.2 ScriptComponent + 生命周期**：挂进 EnterPlay S4 + Play tick OnUpdate + Stop；
   一个 demo 脚本让实体在 Play 时移动（dogfood：Play 看实体动、Stop 还原）。
4. **B1.3 Inspector tweakable**：schema 从 assembly metadata 反射 public field。
5. **B1.4 热重载**：可卸载 ALC + file watch + 重置状态（dogfood：改脚本存盘、Play 中即时生效）。

每步先 headless 验证能验的部分（host/binding 返回值、序列化 round-trip），运行时行为
（Play 中脚本驱动、热重载即时性）登记到 `docs/dogfood-checklist.md` 人工 dogfood。

## 9. 待用户裁定的开放问题

- **运行时 host**：CoreCLR（推荐，现代 .NET + 热重载）vs Mono（Unity 同款，domain 干净）？
- **assembly 构建**：游戏 C# 工程用 `dotnet build` 出 `.dll`，编辑器侧触发还是用户手动？
- **SDK 边界**：`OrangeScript` 基类 + 绑定 API 放哪个仓（引擎出 C# SDK 包？OrangeGames 引用？）。
- **第一批绑定面**：MVP 暴露哪些引擎能力（transform/input/instantiate/物理查询/动画参数）。

> 与 B2 动画时序编辑的关系：脚本可读写 animator 参数（引擎已有 ProceduralAnimator /
> AnimationStateMachine runtime），两条线在「脚本驱动动画」处交汇，但可独立推进。
