#ifndef ORANGE_ENGINE_ANIMATION_CLIP_ANIMATOR_H
#define ORANGE_ENGINE_ANIMATION_CLIP_ANIMATOR_H

// ---------------------------------------------------------------------------
// ClipAnimator —— IAnimator 的 Transform 驱动后端（数据关键帧 → 实体本地位姿）。
//
// 与 ProceduralAnimator 配对：后者把"时间 → 曲线值"写进 MaterialInstance 的
// shader uniform；ClipAnimator 把同样的数据曲线（AnimationClip / AnimationTrack /
// Keyframe，见 AnimationClip.h）写进一个 TransformComponent 的 position /
// rotation / scale。这是 B2 动画时序编辑的运行时消费端：timeline / 曲线编辑
// 产出 AnimationClip 数据，ClipAnimator 在 Play 模式逐帧采样应用。
//
// 与 hierarchy 的耦合：ClipAnimator 只写**本地** TRS。写完后 TransformSystem 的
// 每帧 PropagateWorldTransforms（ADR-016）照常累积父变换 → 被动画的实体若有子
// 节点，子节点自然跟随。ClipAnimator 不需要知道 world，与"消费者读 world cache"
// 的 A1 设计正交。
//
// 写目标的持有方式沿用 ProceduralAnimator：持一个非拥有 TransformComponent*，
// 由调用方 SetTarget 接上。生命周期约定与 ProceduralAnimator 的 MaterialInstance*
// 一致——调用方保证 target 在 animator 存活期内有效（典型：animator 与 target
// 同属一个 entity，entity 销毁时 AnimatorComponent 先析构）。EnTT 默认 paged
// storage 对单 component 指针稳定（增删**其它** entity 不搬动本 component），
// 故跨帧持指针在 0.x 阶段安全；若 target 那条 component 被显式 remove 则失效，
// 调用方需重新 SetTarget。
//
// targetName 约定：每条 AnimationTrack 的 targetName 决定它驱动 Transform 的哪个
// 字段，由 ParseTransformTarget 解析（见下）。未识别的 track 静默跳过——让一个
// clip 里混入非 Transform 通道（将来可能驱动别的东西）不报错。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/animation/AnimationClip.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/scene/TransformComponent.h>

#include <string_view>

namespace Orange::Engine::Animation
{

// 一条 track 驱动 TransformComponent 的哪个字段。
enum class TransformTarget
{
    Unknown,       // targetName 未识别 → ClipAnimator 跳过该 track
    Position,      // Vec3 全量 → position
    PositionX,     // 标量 → position.x
    PositionY,     // 标量 → position.y
    PositionZ,     // 标量 → position.z
    RotationEuler, // Vec3（角度制 XYZ）→ rotation（合成单位四元数）
    Scale,         // Vec3 全量 → scale
    ScaleX,        // 标量 → scale.x
    ScaleY,        // 标量 → scale.y
    ScaleZ,        // 标量 → scale.z
    ScaleUniform,  // 标量 → scale 三轴同值
};

// 解析 track.targetName → TransformTarget。约定名（大小写敏感）：
//   "position" / "position.x" / "position.y" / "position.z"
//   "rotation"（等价 "rotation.euler"，Vec3 角度制）
//   "scale" / "scale.x" / "scale.y" / "scale.z" / "scale.uniform"
// 其它 → Unknown。标量字段读采样 vec4 的 .x 维（Float track 的值落在 .x）。
ORANGE_ENGINE_API TransformTarget ParseTransformTarget(std::string_view targetName) noexcept;

class ORANGE_ENGINE_API ClipAnimator final : public IAnimator
{
public:
    // clip 按值拷入并被 animator 拥有（自包含、可独立测试，与 ProceduralAnimator
    // 的 AddDataChannel 按值捕获 track 同一心智）。target 可为 nullptr——半构造态，
    // Tick 仍推进 elapsed、只是不写任何字段，之后 SetTarget 接上即可。
    // 构造时若 clip.duration <= 0，自动用 ComputeClipDuration 从关键帧推出时长。
    explicit ClipAnimator(AnimationClip clip = {}, Scene::TransformComponent* target = nullptr);
    ~ClipAnimator() override;

    ClipAnimator(const ClipAnimator&)            = delete;
    ClipAnimator& operator=(const ClipAnimator&) = delete;
    ClipAnimator(ClipAnimator&&)                 = delete;
    ClipAnimator& operator=(ClipAnimator&&)      = delete;

    // 切换写目标。nullptr → 暂停写入（Tick 仍推进 elapsed）。
    void                       SetTarget(Scene::TransformComponent* target) noexcept;
    Scene::TransformComponent* GetTarget() const noexcept;

    // 替换 clip（duration<=0 时自动从关键帧推出），并把 elapsed 夹回新 clip 时间域。
    void                 SetClip(AnimationClip clip);
    const AnimationClip& Clip() const noexcept;

    // 来源 .anim 资产路径（空 = 内联构造、非来自资产）。非空时 scene 序列化只存
    // 引用（clipSource）而非内联整个 clip（clipJson），与 RenderableComponent.mesh
    // 同款"scene 存路径、资产文件持数据"，避免双源真相。编辑器拖 .anim 进 Inspector
    // 时设置（见 docs/b2.6-animator-clip-authoring-spec.md 改点 1/3）。
    void             SetSourceAssetPath(std::string_view path);
    std::string_view SourceAssetPath() const noexcept;

    // 播放控制。Play/Pause 只切 mPlaying（Tick 据此决定是否推进）。
    void  Play() noexcept;
    void  Pause() noexcept;
    void  Stop();  // mPlaying=false + elapsed=0 + 立即应用 t0 pose
    bool  IsPlaying() const noexcept;
    void  SetLoop(bool loop) noexcept;
    bool  IsLooping() const noexcept;
    void  Seek(float seconds);  // 设 elapsed（按 loop/clamp wrap）+ 应用 pose
    float ElapsedSeconds() const noexcept;
    float Duration() const noexcept;

    // 按当前 elapsed 把 clip 采样应用到 target（不推进时间）。target==nullptr 安全
    // no-op。供编辑器 timeline scrubbing / 显式重应用用。
    void ApplyPose() const;

    // IAnimator
    // mPlaying 时：elapsed = Wrap(elapsed + dt)，再 ApplyPose。否则不动。
    void             Tick(float dt) override;
    // 非 loop 且 elapsed 到达 duration → true；loop clip 永远 false。
    bool             IsFinished() const noexcept override;
    std::string_view BackendName() const noexcept override;

private:
    AnimationClip              mClip;
    Scene::TransformComponent* mpTarget{nullptr};
    float                      mElapsedSeconds{0.0f};
    bool                       mPlaying{true};
    std::string                mSourceAssetPath;  // 空 = 内联 clip（见 SourceAssetPath 注释）
};

}  // namespace Orange::Engine::Animation

#endif  // ORANGE_ENGINE_ANIMATION_CLIP_ANIMATOR_H
