# Case Study：流体角色 + 形态吞噬系统（第一款游戏）

## 状态与范围

- **类型**：游戏侧设计笔记，**不是引擎规约**
- **目的**：记录第一款游戏（暂未命名）核心玩法机制的设计思路与技术拆解，作为 OrangeEngine 扩展点（参见 [`extension-points.md`](../extension-points.md)）的下游消费样例
- **位置**：本文件**仅作为参考**驻留在引擎仓库 `docs/case-studies/`。当游戏仓库独立 fork 出来后，本文件应迁移到游戏仓库的 `docs/` 下，引擎仓库这里保留一个 stub + 链接
- **不会做的事**：本文件不会引入引擎层的 `CharacterForm` / `TransformationSystem` 类型；这些是游戏仓库的概念

## 玩法基线

- 主角默认形态：流体生物（视觉灵感参考《转生史莱姆》，但保留向"外形流体生物"风格演化的开放性）
- 探索机制：在关卡中击败 Boss 后，可吞噬其形态
- 战斗机制：吞噬后，可在后续探索 / 战斗中切换为该 Boss 形态
- 形态切换：常态是流体形态，玩家主动触发"变身为 X"或"变回流体"
- 切换过程：保持可控移动 + 给若干无敌帧（即非 cutscene 锁屏）

## 关键设计观察：Slime 是 Hub 拓扑

```
                 BossForm_A
                     ↕
BossForm_C   ⇌   Slime   ⇌   BossForm_B
                     ↕
                 BossForm_D
```

**所有形态切换都经过流体形态中转，永远不存在 Boss A 直接变 Boss B**。

这个设计决定带来的工程红利：
- 形态切换的过渡实现复杂度从 **O(N²)** 降到 **O(N)**
- 永远只需要实现 `Slime ⇌ AnyBossForm` 一种过渡
- 视觉过渡的"中间态"恰好就是默认形态——叙事与技术对齐
- 玩法上自然产生 cooldown / 缓冲（如果切换需要消耗资源或时间，流体作为天然中介）

**这是 game design 锁定的核心约束**——后续不应改成 N×N 多对多直接切换。如果将来确实需要"快速连续切换不同 Boss 形态"，应该设计为"快速过 Slime 中转"的 perceptual hack（极短的中间态），而不是真正的 N² 直接过渡。

## 视觉调性

| 元素 | 实现策略 |
| --- | --- |
| 半透明本体 | alpha ~0.75，标准 alpha blend |
| 表面张力 / 弹性 | 顶点 shader simplex noise 时间扰动；振幅与 character velocity 关联 |
| 湿润光泽 | fragment fresnel + 锐利 GGX 高光（roughness ~0.05–0.1） |
| 内部光感 | 第二 pass：mesh 放大 1.05、模糊 + 加色，作 inner-glow halo |
| 接地阴影 | pivot 下方软边圆形 sprite，按高度 fade alpha |
| 折射（可选） | framebuffer copy + UV 法线偏移采样；性能与排序代价大，初版不做 |
| 眼睛 / 表情 | 单独 mesh 或 sprite 层，UV 切换不同表情，与身体 shader 解耦 |

**艺术风格演化的余地**：将来从"史莱姆"变更为"外星流体生物"调性时，只需替换 fragment shader 颜色 LUT 与内部纹理；不需要动几何、不需要动骨骼。

## 三个独立子问题的工程拆解

### 子问题 A：Boss 形态的造型与动作

- **类别**：标准 2D 骨骼动画
- **工具**：DragonBones runtime（`SkeletalAnimator`）
- **资产**：每个 Boss = 一个 DragonBones armature + 若干 animation clip（idle / attack / hurt / death）
- **引擎依赖**：`Orange::Engine::Animation::SkeletalAnimator`（Phase 4 交付）
- **复杂度**：低；DragonBones 标准能力

### 子问题 B：流体主角的视觉表达

- **类别**：shader-driven 程序化角色
- **工具**：自定义 vertex + fragment shader 走 `MaterialSystem::RegisterTemplate` 注入
- **资产**：低多边形 blob mesh + inner noise texture + 注册表中的 "slime_fresnel" template
- **引擎依赖**：`Orange::Engine::Render::MaterialSystem` 自定义 shader 注入（Phase 3 交付）+ `Orange::Engine::Animation::ProceduralAnimator` 驱动 shader uniform（Phase 4 交付）
- **复杂度**：中；shader 编写本身不复杂，但需要美术调试 noise 频率、fresnel 颜色等参数
- **关键洞察**：这**不是动画库的问题**——任何 2D 骨骼动画库都做不出真正的流体感。这是 shader 问题，归 Render 模块管，DragonBones 不参与

### 子问题 C：跨形态变形 + 可控移动 + 无敌帧

- **类别**：multi-system 协调
- **构成**：
  1. **Dissolve out**：当前 Boss form 的 mesh 用 dissolve shader（noise mask + alpha 阈值）渐次溶解，碎片化为粒子飞向角色 pivot
  2. **Slime 过渡态**：粒子全部聚拢，`ProceduralAnimator` 接管渲染，shader-driven blob 形态，过程持续 0.3–1.0s
  3. **Coalesce in**：目标形态的 mesh 用反向 dissolve 显形，从体内向外渐次 alpha 露出 + emission 退色
  4. **物理碰撞器替换**：变形前销毁旧 Box2D fixture，挂临时 capsule 兜底，变形结束后挂目标形态的 fixture（依赖引擎 `PhysicsWorld::ReplaceFixture`，Phase 4 交付）
  5. **Hurtbox 关闭 / Hitbox 保留**：通过 collision layer bits 实现无敌帧（hurtbox layer disable，hitbox layer 不变）
  6. **InputContext 切换**：变形期间禁用攻击 / 特殊技能，保留水平移动；通过 InputContext stack push 一个 "Transforming" context

- **引擎依赖**：
  - `MaterialSystem::RegisterTemplate`（dissolve shader）
  - `ProceduralAnimator`（slime 中间态）
  - `SkeletalAnimator`（Boss 形态）
  - `PhysicsWorld::ReplaceFixture`
  - `InputContext` stack

- **完全位于游戏仓库的代码**：
  - `CharacterForm` 数据结构（一个形态的全部 metadata：skeleton / shader template / collider / movement params / abilities）
  - `TransformationSystem`（ECS system）：监听变形请求 → 编排上述六步 → 写回 ECS state
  - `FormRegistry`（游戏侧 asset 类型，通过 `AssetRegistry::Register<FormAsset>` 注入引擎）

## 游戏侧伪代码（落在游戏仓库）

```cpp
// 游戏仓库：components/CharacterFormComponent.h
struct CharacterFormComponent
{
    AssetHandle<FormAsset> mCurrentForm;       // 当前形态的全部参数
    AssetHandle<FormAsset> mTargetForm;        // 变形目标，仅在 Transforming 状态有效
    float                  mTransformProgress; // 0.0 → 1.0
    bool                   mIsInvincible;      // 无敌帧标志
};

// 游戏仓库：systems/TransformationSystem.cpp
void TransformationSystem::Update(World& world, const FrameContext& ctx)
{
    auto* pRegistry = static_cast<entt::registry*>(world.GetNativeRegistry());

    auto view = pRegistry->view<CharacterFormComponent, TransformationRequestEvent>();
    for (auto entity : view) {
        auto& form    = view.get<CharacterFormComponent>(entity);
        auto& request = view.get<TransformationRequestEvent>(entity);

        // Phase 1: dissolve out 当前形态
        DispatchDissolveOutVfx(entity, form.mCurrentForm);

        // Phase 2: 切换到 procedural slime renderer
        SwapToSlimeAnimator(entity);

        // Phase 3: 物理 fixture 临时替换为 capsule
        Engine::Physics::ColliderDesc capsule = MakeBoundingCapsule(form.mCurrentForm, request.mTargetForm);
        mpPhysicsWorld->ReplaceFixture(entity, capsule);

        // Phase 4: 启用无敌（关闭 hurtbox layer）
        form.mIsInvincible = true;
        DisableHurtbox(entity);

        // Phase 5: 输入上下文切换
        mpInputManager->PushContext("Transforming");

        // Phase 6 (在 mTransformProgress 推进至 1.0 后，下一帧触发):
        //   - SwapToSkeletalAnimator(entity, request.mTargetForm)
        //   - DispatchCoalesceInVfx(entity, request.mTargetForm)
        //   - ReplaceFixture(entity, GetColliderForForm(request.mTargetForm))
        //   - PopInputContext()
        //   - form.mIsInvincible = false

        form.mTargetForm = request.mTargetForm;
        form.mTransformProgress = 0.0f;
        pRegistry->remove<TransformationRequestEvent>(entity);
    }

    // 推进进度（独立循环）
    auto progressView = pRegistry->view<CharacterFormComponent>();
    for (auto entity : progressView) {
        auto& form = progressView.get<CharacterFormComponent>(entity);
        if (form.mTransformProgress < 1.0f) {
            form.mTransformProgress += ctx.mDeltaTime / kTransformDuration;
            if (form.mTransformProgress >= 1.0f) {
                FinalizeTransformation(entity, form);
            }
        }
    }
}
```

## 引擎扩展点对照清单

游戏侧实现这套机制需要的引擎能力（按 Phase 排序）：

| 引擎能力 | 用途 | Phase |
| --- | --- | --- |
| EnTT 组件 / 系统注册 | `CharacterFormComponent` / `TransformationSystem` | Phase 1 |
| Asset 类型注册 | `FormAsset` / `.form` 资源类型 | Phase 2 |
| `MaterialSystem::RegisterTemplate` | slime shader / dissolve shader 注入 | Phase 3 |
| `ProceduralAnimator` 驱动 shader uniform | slime 中间态视觉 | Phase 4 |
| `SkeletalAnimator` (DragonBones) | Boss 形态骨骼动画 | Phase 4 |
| `PhysicsWorld::ReplaceFixture` | 形态切换时碰撞器替换 | Phase 4 |
| `InputContext` stack | 变形期间限制输入 | Phase 4 |
| Collision layer / mask | 无敌帧（hurtbox 关闭） | Phase 4 |
| VFX 粒子系统 + dissolve shader | dissolve in/out 效果 | Phase 5 |

**至 Phase 5 结束时，引擎层应有的能力刚好覆盖这套机制**——这是 Phase 5 是"游戏 fork 时机"的依据。

## 风险与开放问题

### 已知风险
- **DragonBones C++ runtime 维护活跃度低**：Boss 形态依赖 DragonBones，runtime 出现严重 bug 时需要 fork 维护或换 Spine（商用授权）。引擎 `IAnimator` 抽象保留这个换实现的可能性
- **Procedural slime shader 美术调试成本未知**：rim light + fresnel + noise 的参数组合可能需要大量美术 iteration；编辑器 v0.1（Phase 6）的材质子模式可能需要前移
- **Fixture 替换的物理稳定性**：Box2D 销毁/重建 fixture 时如果与碰撞同帧，可能产生瞬间穿透。需要在引擎 Phase 4 实现时充分测试

### 开放问题（需游戏开发推进时再定）
- 形态有最大数量上限吗？（例如最多吞噬 5 个 Boss）这影响 UI 与切换 UX
- 形态间是否有"配方"机制？（例如吞噬 Boss A + B 解锁 C 形态）
- 是否允许"半 boss / 半流体"的混合形态？如果有，引擎需要支持 mesh 拼装 / 部件级替换——这是 Phase 5+ 才能讨论的能力
- 流体形态本身有没有独立的攻击 / 移动技能？（如果是 hub-only，玩法薄弱；如果有独立技能，需要专门设计）
- 变形 cooldown 是基于时间还是资源（例如吞噬 boss 时获得的能量条）？

## 与引擎 roadmap 的对接

- 任何 game-specific 概念**永不进入** [`docs/design-plan.md`](../design-plan.md) 与 [`docs/roadmap.md`](../roadmap.md)
- 引擎 PR 评审中如果出现 "Slime" / "Form" / "Boss" / "Player" 等游戏术语，视为架构 bug，必须重构掉再合并
- 本文件随游戏仓库 fork 出去后，引擎仓库保留一个 stub：仅留指向游戏仓库 `docs/character-forms.md` 的链接，正文内容删除
