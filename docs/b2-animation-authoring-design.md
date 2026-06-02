# Phase B2 设计提案：动画时序编辑（数据模型 + timeline）

> 状态：**设计提案（未实现）**。用户 2026-06-02「PIE + 动画时序编辑」重点排期下的 B2 地基调研。
> 与 `docs/maturity-roadmap.md` B2.1~B2.6 配套——本文聚焦**最该先做的 B2.1 数据模型**（headless
> 可建可测，是所有 timeline/curve UI 的根基），并校正 B2.5 现状。落地前关键决策升 ADR。

## 1. 现状精确盘点（读真实头文件后）

引擎动画 **runtime 齐全但创作侧分裂成两种"非数据"形态**：

| 子系统 | 现状 | 创作侧缺口 |
|---|---|---|
| `ProceduralAnimator` | channel = `std::function<T(float)>`（**C++ lambda**，`ProceduralAnimator.h:75`）；T∈{float,int,vec2-4,mat4}；写 MaterialInstance uniform | **曲线是代码不是数据**——编辑器无法编、无法序列化 |
| `SkeletalAnimator` | DragonBones 后端（数据，但是 DragonBones 私有格式） | 非引擎原生 clip，editor 难统一编 |
| `AnimationStateMachine` | **已有数据驱动路径**：`ConditionExpr{param,op,threshold}` 可序列化 `.anim_fsm`（ADR-005 v0.7），有 `AnimFsmAssetInspectorPlugin` | ✅ 数据模型 + 列表式 inspector 已存在；**仅缺节点图 UI**（B2.5 比 roadmap 说的更完成） |

**结论**：B2 的真正地基缺口 = **引擎原生、可序列化的动画 clip（keyframe + curve）**，把 Procedural
channel 从 lambda 升级成数据，让编辑器能编。FSM 那条已有数据底座，B2.5 只是补可视化图 UI。

## 2. B2.1 动画 clip 数据模型（最先做，headless 可测）

### 数据结构（schema-first，可序列化 `.anim`）

```text
AnimationClip {
  name        : string
  duration    : float            // 秒
  loop        : bool
  tracks      : Track[]
}
Track {
  targetKind  : enum { MaterialUniform, TransformPos, TransformRot, TransformScale, Custom }
  targetName  : string           // uniform 名 / 组件字段路径
  valueType   : enum { Float, Vec2, Vec3, Vec4 }
  keys        : Keyframe[]        // 按 time 升序
}
Keyframe {
  time        : float
  value       : Vec4             // 按 valueType 用前 N 维
  interp      : enum { Step, Linear, Bezier }
  inTangent   : Vec2            // Bezier 时用（dx,dy）
  outTangent  : Vec2
}
```

### 采样算法（语言无关，headless 可测）

```text
sample(track, t):
  找到包住 t 的相邻 key k0,k1（二分；t < first → first.value；t > last → last.value）
  u = (t - k0.time) / (k1.time - k0.time)
  switch k0.interp:
    Step   : return k0.value
    Linear : return lerp(k0.value, k1.value, u)
    Bezier : return cubicBezier(k0.value, k0.outTangent, k1.inTangent, k1.value, u)
```
复杂度：每 track 每帧 O(log N) 二分（key 数 N）。不变量：keys 按 time 严格升序（编辑器插入时维持）。

### 与现有 animator 接通（不推翻 runtime）

给 `ProceduralAnimator` 加一条**数据 channel** 路径（与现有 lambda channel 并存，类比
AnimationStateMachine 的 lambda/数据双路径）：`AddDataChannel(name, const AnimationTrack&)`
内部包成 `fn = [track](float t){ return sample(track, t); }` 存进同一 `mChannels` 容器——
**runtime 零改动**，Tick 仍只调 `fn(elapsed)`。Transform track 同理喂给一个新的轻量
`ClipAnimator`（写 entity TransformComponent，A1 的 hierarchy 传播自动让子跟随）。

**这一步全程 headless 可测**：构造 clip → sample 在关键时间点断言值（step/linear/bezier
各一例）→ 喂 ProceduralAnimator 验 uniform 写对。无 GUI、无 GPU 依赖——像 A1 地基一样先落。

## 3. B2.3/B2.4 Timeline + 曲线编辑器（UI，需 dogfood）

数据模型落地后，编辑器 Animation tab（当前 placeholder）建：
- **轨道列表 + dopesheet**：每 Track 一行，keyframe 画成可拖菱形；顶部时间标尺 + playhead
  scrub（拖 playhead 实时 sample 驱动选中实体预览——复用 ProceduralAnimator 实时可见，
  pbr 材质已接 UBO）。
- **曲线编辑器**：选中 track 切曲线视图，Bezier handle 可拖（in/out tangent）。
- 参照 `vendor/cocos-engine` 的 Animation 面板布局 + `vendor/LumixEngine` 的实现路径
  （编辑器纪律：先看 Lumix `src/editor` 对应实现再以自家约定重写，不直接抄）。
- 录制/插入 keyframe：选实体 + 改字段 + 在 playhead 处「K」打键（Unity 同款）。

UI 行为（拖 key、scrub 手感、曲线 handle）**GUI 无法 headless 验证**，登记
`docs/dogfood-checklist.md` 人工 dogfood——参 transform gizmo 同款教训（读代码判"能用"会翻车）。

## 4. B2.5 状态机图编辑器（数据底座已就绪，只补 UI）

`.anim_fsm` 数据格式 + `ConditionExpr` + `AnimFsmAssetInspectorPlugin` 已存在（ADR-005）。
B2.5 = 在此之上加**节点图可视化**（状态=节点、transition=边、点边看/编 ConditionExpr），
不重做数据层。节点图 UI 是纯 ImGui 绘制 + 命中测试（类比 transform gizmo 的交互数学），
GUI dogfood。

## 5. B2.2 GPU skinning（跨仓，独立推进）

骨骼蒙皮上 GPU = bone matrix palette + vertex skinning shader，**属 OrangeRender**。按 ADR-009
三段式（OrangeRender session 落地 → umbrella bump → 引擎消费），与本文 B2.1/B2.3 数据/UI 线
正交，可并行。当前骨骼动画在 viewport 看不到形变的根因就是缺它。

## 6. 实施顺序（与 roadmap B2 对齐 + 细化）

1. **B2.1 数据模型 + 采样**（引擎层，headless 可测）——**先做，最高杠杆**：所有 UI 的根基，
   且像 A1 一样纯逻辑可自主推进 + 不需 dogfood。
2. **B2.6 AnimatorComponent 可 Add-Component**（schema 注册 + 绑 clip/fsm）——让 animator 能在
   编辑器挂上。
3. **B2.3 timeline/dopesheet UI** + **B2.4 曲线编辑器**（dogfood）。
4. **B2.5 状态机图 UI**（数据已就绪，补节点图，dogfood）。
5. **B2.2 GPU skinning**（跨仓 OrangeRender，可与上面并行）。

## 7. 待用户裁定

- `.anim` clip 是否复用现有 `Core::Serialization`（JsonWriter/BinaryWriter）+ schema_version 冻结纪律（应该是）。
- Transform track 的 `ClipAnimator` 写 local 还是 world（写 local，靠 A1 hierarchy 传播——与 A1 一致）。
- 录制工作流的 key 粒度（整 transform 一把 key vs 分 pos/rot/scale 三 track）。
- 曲线默认 interp（建议 Linear 默认、可切 Bezier）。
