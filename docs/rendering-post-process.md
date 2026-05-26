# 后处理管线参考（Post-Process Pipeline）

OrangeEngine 前向渲染的屏幕空间后处理栈参考。信息此前散在 `PostProcessPasses.h`
注释、`PipelineImpl.h` 成员、`Pipeline.cpp` 录制顺序里，本页汇成整体视图。

> 权威 source 仍是代码。本页若与代码冲突，以 `src/render/` 为准。

## 录制顺序

主 HDR pass + 粒子之后、tonemap/passthrough 之前，`Pipeline.cpp` 的 `Render`
（窗口）与 `RenderOffscreen`（编辑器视口）**两路径顺序一致**：

```
主 pass → 粒子
  → SSAO/GTAO      (乘法 blend，凹处变暗)
  → SSR            (加性，屏幕空间反射)
  → 接触阴影        (乘法，仅 directional light)
  → DoF            (CoC 圆盘 gather)
  → Bloom          (mip 链)
  → God Rays       (加性光柱，bloom 之后)
  → TAA resolve    (jitter + 历史重投影；jittered viewProj)
  → Sharpen        (CAS，恢复 TAA 软化)         ← 紧接 TAA
  → Motion Blur    (相机重投影速度；未 jitter baseViewProj)
  → Color Grade    (曝光/白平衡/对比/饱和)
  → Lens           (色散 + 暗角；最后的"镜头"阶段)
  → tonemap / passthrough → swapchain / viewportColor
```

顺序要点：
- **Sharpen 紧接 TAA**：TAA 多帧 resolve 会软化，sharpen 恢复高频；放在 motion
  blur / DoF 之前——那些是有意虚化，锐化其结果无意义。
- **Motion blur 用未 jitter 的 `baseViewProj`** 算屏幕速度（不含 TAA 亚像素抖动）；
  TAA 用 jittered viewProj。两者各取所需。
- **接触阴影 / SSAO 乘法暗化在加性发光（god rays / bloom）之前**。
- overlay（grid / aux / debug draw）用**未 jitter 的 baseViewProj**，不进 TAA
  resolve，否则逐帧 sub-pixel shimmer。

## 两种激活路径：Chain vs Component

每个 pass 的 `FindActiveXxxPass()` 是二选一：

1. **PostProcessComponent（数据驱动，优先）**：场景里挂了 `PostProcessComponent`
   时 `SyncPostProcessFromWorld` 每帧把组件字段灌进 `Pipeline::Impl::postXxx` 成员，
   置 `postComponentActive=true`；`FindActive*` 走 `postXxx.enabled`。**find-first 全局
   单例**语义（v1）。编辑器 Inspector 自动出控件（schema 驱动）、随场景存盘。
2. **PostProcessChain（退回）**：无组件时 `FindActive*` 退回 chain 的
   `dynamic_cast<XxxPass*>`。sample / 测试用这条；编辑器 ScenePanel 也建了默认 chain
   作"无组件默认观感"。

> 二者互斥：组件在场即覆盖 chain。

## PostProcessComponent 覆盖范围

`include/orange/engine/render/PostProcessComponent.h`（扁平字段，schema-first 要求
直接成员）。覆盖：SSAO（含 GTAO）/ SSR / 接触阴影 / DoF / TAA / 色彩分级 / **相机
运动模糊** / **镜头（色散+暗角）** / **锐化** + PCSS 软阴影 + shadow map 分辨率。

**不含 bloom / tonemap**——那两个与 HDR→LDR stage-A/B 收尾耦合，仍由默认 chain 管。

volume 容器字段（mode/localExtent/priority/blendDistance）是 **v2 局部 volume 占位**，
v1 只走 Global find-first。v2（相机位置混合）解锁时零 schema 改动。

序列化：`ComponentSerializers.cpp`（全字段 optional，向后兼容）；scene schema 当前
`{"scene/world", 1, 10}`，每加一组字段 +1 minor。

## 实现模板（加新 pass）

见 memory `reference_orangeengine_new_postprocess_pass_recipe`，或抄 `PipelineDof.cpp`
/ `PipelineLens.cpp`：gather（读 hdrColor + ubo → 独立 `<x>Color` RGBA16F）→ composite
（**复用 `dofCompositePipeline`** replace blend 写回 HDR）。独立 target 避免读写同
target 反馈。

**已知坑**：
- descriptor set 必须 **allocate-once + UpdateDescriptorSet**，绝不 resize 时
  reset+realloc（RHI pool 无 free-bit，累积耗尽 OOM → 编辑器 4K resize 崩溃）。
- 屏幕 2D 方向抬成 view-space 时 `omega.y` 取负（主 pass `p[1][1]=-f` 的 y-flip）。

## 测试

- `tests/render/ShadowOcclusionTest.cpp`：offscreen-post smoke（chain 路径，挂全套
  pass + resize-churn 压 pool）+ **组件驱动 post**（component 路径，验
  `SyncPostProcessFromWorld → postComponentActive` 端到端出图）。
- `tests/scene/PostProcessRoundTripTest.cpp`：组件序列化往返逐字段。

## 下一步

CSM（级联阴影）——directional 阴影质量，见 `docs/engine-known-gaps.md` 的
`GAP-2026-05-27-cascaded-shadow-maps`（留专门 session）。
