#ifndef ORANGE_ENGINE_ANIMATION_BLEND_SPACE_H
#define ORANGE_ENGINE_ANIMATION_BLEND_SPACE_H

// ---------------------------------------------------------------------------
// BlendSpace —— 参数化姿势混合（blend space / blend tree）的纯数据 + 采样原语。
//
// 背景：ClipAnimator 只能播放单个 clip；CrossFadeTo 做的是**两个 clip 随时间**
// 的过渡混合。locomotion 需要的是另一种混合——按一个连续参数（如 speed）在
// idle / walk / run 之间**空间上**平滑插值，且各 clip 的 phase（步态相位）保持
// 同步。这就是 blend space：把 (参数 → 姿势) 的映射表示成一组"带坐标的样本
// clip"，运行时对邻近样本采样后加权混合。
//
// 本头是 header-only 的纯算法层（无 GPU / 无 OrangeRender / 无 IAnimator 依赖），
// headless 可测。运行时后端见 BlendSpaceAnimator（把 1D blend space 接成
// IAnimator，逐帧推进 phase + 按外部参数采样写 TransformComponent）。数据原语
// （BlendSpace1D/2D + BlendPoses + ApplyAdditivePose）本身可被游戏 / 未来 2D
// animator / FSM 状态内混合层直接复用。
//
// 与 FSM 的关系：blend space 是"状态内的连续混合层"，与 AnimationStateMachine
// 的"状态间离散切换"正交——FSM 的某个 locomotion 状态 OnEnter 挂一个
// BlendSpaceAnimator，每帧把 gameplay 的 speed 喂进 SetBlendParameter。
// ---------------------------------------------------------------------------

#include <orange/engine/animation/AnimationClip.h> // AnimationClip / SampleTrack
#include <orange/engine/animation/ClipAnimator.h>  // ParseTransformTarget / TransformTarget
#include <orange/engine/scene/TransformComponent.h>

#include <glm/common.hpp>         // glm::clamp / glm::mix
#include <glm/geometric.hpp>      // glm::length（vec2/vec4 模长）
#include <glm/gtc/quaternion.hpp> // glm::quat / glm::slerp / glm::dot / glm::inverse / glm::normalize
#include <glm/trigonometric.hpp>  // glm::radians（RotationEuler 度→弧度）
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <vector>

namespace Orange::Engine::Animation
{

    // 把 clip 在时间 time 的采样姿势写进 pose（只覆写 clip 驱动的字段，未驱动字段保持
    // pose 原值）。这是 ClipAnimator::SampleClipPose 的 header-only 版——遍历 tracks，
    // ParseTransformTarget（导出符号，inline 调用 OK）→ SampleTrack → 按目标字段写。
    // blend space 的每个样本 clip 先各自采样进一份 baseline 副本再交给 BlendPoses 混合。
    inline void SampleClipIntoPose(const AnimationClip& clip, float time,
                                   Scene::TransformComponent& pose)
    {
        for (const AnimationTrack& track : clip.tracks)
        {
            const TransformTarget field = ParseTransformTarget(track.targetName);
            if (field == TransformTarget::Unknown)
            {
                continue;
            }

            // SampleTrack 返回 vec4：Vec3/Vec2 track 取前 N 维，标量（Float track）落在 .x。
            const glm::vec4 v = SampleTrack(track, time);

            switch (field)
            {
                case TransformTarget::Position:
                    pose.position = glm::vec3(v);
                    break;
                case TransformTarget::PositionX:
                    pose.position.x = v.x;
                    break;
                case TransformTarget::PositionY:
                    pose.position.y = v.x;
                    break;
                case TransformTarget::PositionZ:
                    pose.position.z = v.x;
                    break;
                case TransformTarget::RotationEuler:
                    // Vec3 角度制 → 弧度 → 合成四元数（glm 按 vec3 构造的固定欧拉序）。
                    pose.rotation = glm::quat(glm::radians(glm::vec3(v)));
                    break;
                case TransformTarget::RotationQuat:
                    // Quat track：SampleTrack 已在四元数空间走最短弧 slerp + normalize，
                    // 返回 vec4(x,y,z,w)；直接构造 glm::quat（构造取 (w,x,y,z)）写入。
                    pose.rotation = glm::quat(v.w, v.x, v.y, v.z);
                    break;
                case TransformTarget::Scale:
                    pose.scale = glm::vec3(v);
                    break;
                case TransformTarget::ScaleX:
                    pose.scale.x = v.x;
                    break;
                case TransformTarget::ScaleY:
                    pose.scale.y = v.x;
                    break;
                case TransformTarget::ScaleZ:
                    pose.scale.z = v.x;
                    break;
                case TransformTarget::ScaleUniform:
                    pose.scale = glm::vec3(v.x);
                    break;
                case TransformTarget::Unknown:
                    break; // 上面已 continue
            }
        }
    }

    // N-pose 加权混合：把 count 个姿势按 weights 加权混合进 out。CrossFadeTo 的两-pose
    // 混合数学（position/scale 线性、rotation 走符号对齐的归一化加权四元数和）泛化到 N。
    //   * count==0 → out = 默认 TransformComponent（identity）。
    //   * count==1 → out = poses[0]。
    //   * sumW <= eps → 退化，out = poses[0]（权重全零/负和防除零）。
    //   * 否则各 pose 按 weights[i]/sumW 归一化加权。
    // rotation 用 nlerp for N：以 poses[0] 为符号参考，其余 quat 若与参考点积为负则取反
    // （保证在同一半球，避免走远弧），归一化加权求和后再 normalize。这是 slerp 对 N-pose
    // 的标准近似（多路混合无闭式 slerp）；相邻姿势夹角小时误差极小。若加权和近零（反极点
    // 抵消的病态输入）回退符号参考 quat。
    inline void BlendPoses(const Scene::TransformComponent* poses, const float* weights,
                           std::size_t count, Scene::TransformComponent& out)
    {
        if (count == 0)
        {
            out = Scene::TransformComponent{};
            return;
        }
        if (count == 1)
        {
            out = poses[0];
            return;
        }

        float sumW = 0.0f;
        for (std::size_t i = 0; i < count; ++i)
        {
            sumW += weights[i];
        }
        if (sumW <= 1e-8f)
        {
            out = poses[0];
            return;
        }

        glm::vec3       accPos(0.0f);
        glm::vec3       accScale(0.0f);
        glm::vec4       accRot(0.0f);
        const glm::quat qRef = poses[0].rotation; // rotation 符号对齐参考
        for (std::size_t i = 0; i < count; ++i)
        {
            const float w = weights[i] / sumW;
            accPos += w * poses[i].position;
            accScale += w * poses[i].scale;

            glm::quat qi = poses[i].rotation;
            if (glm::dot(qRef, qi) < 0.0f)
            {
                qi = -qi; // 取反邻接四元数 → 与参考同半球，走最短弧
            }
            accRot += w * glm::vec4(qi.x, qi.y, qi.z, qi.w);
        }

        out.position = accPos;
        out.scale    = accScale;

        const float len = glm::length(accRot);
        // accRot 近零 = 反极点权重抵消的病态输入 → 回退符号参考 quat（无良定义的加权均值）。
        out.rotation = (len < 1e-8f) ? qRef
                                     : glm::normalize(glm::quat(accRot.w, accRot.x, accRot.y, accRot.z));
    }

    // 附加层叠（additive blending）：在 base 姿势上叠加 additive 相对 additiveRef 的
    // 差量，按 weight 缩放，写进 out。用于把"呼吸 / 后坐力 / 瞄准偏移"等附加动画叠到
    // locomotion base 上。position/scale 取差量线性叠加；rotation 取"从 ref 到 additive
    // 的差量旋转"按 weight slerp 后右乘 base（局部空间叠加）。
    inline void ApplyAdditivePose(const Scene::TransformComponent& base,
                                  const Scene::TransformComponent& additive,
                                  const Scene::TransformComponent& additiveRef, float weight,
                                  Scene::TransformComponent& out)
    {
        out.position = base.position + weight * (additive.position - additiveRef.position);
        out.scale    = base.scale + weight * (additive.scale - additiveRef.scale);

        // 差量旋转 delta = ref⁻¹ * additive（把 additive 表达成相对 ref 的局部旋转），
        // 按 weight 在 identity→delta 之间 slerp 缩放，右乘 base 得叠加结果。
        const glm::quat delta  = glm::normalize(glm::inverse(additiveRef.rotation) * additive.rotation);
        const glm::quat wDelta = glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), delta, weight);
        out.rotation           = glm::normalize(base.rotation * wDelta);
    }

    // ---- 1D blend space（单参数，最常见 locomotion：speed → idle/walk/run）--------

    // 一个 1D 样本：在参数轴 position 处对应一个 clip。约定 BlendSpace1D.samples 按
    // position 升序且去重（编辑器 / 构造方维持）。
    struct BlendSample1D
    {
        float         position{0.0f};
        AnimationClip clip;
    };

    struct BlendSpace1D
    {
        std::vector<BlendSample1D> samples; // 约定按 position 升序（去重）
    };

    // 在参数轴 x 处对 1D blend space 求值，写进 out。
    //   * phase ∈ [0,1] 是**归一化步态相位**：每个 clip 在 clamp(phase,0,1)*clip.duration
    //     处采样——不同长度的 clip 靠同一相位保持"同步的步态位置"（walk/run 步频不同、
    //     周期不同，但同相位落在同一步态阶段），是 locomotion blend space 的关键。
    //   * x 落在样本之外 → clamp 到端点样本（无外插）。x 在两样本之间 → 线性权重混合
    //     邻接两样本的姿势（BlendPoses 两-pose）。
    //   * baseline 提供各 clip 未驱动字段的基值（clip 只覆写自己驱动的通道）。
    inline void EvaluateBlendSpace1D(const BlendSpace1D& space, float x, float phase,
                                     const Scene::TransformComponent& baseline,
                                     Scene::TransformComponent&       out)
    {
        const std::vector<BlendSample1D>& samples = space.samples;
        const std::size_t                 n       = samples.size();
        const float                       p       = glm::clamp(phase, 0.0f, 1.0f);

        if (n == 0)
        {
            out = baseline;
            return;
        }

        // NaN 防护：NaN 的 x 会击穿下面所有比较守卫（NaN 参与比较恒 false），令 bracketing
        // while 循环把 i 递进到越界（samples[i+1] 读越界堆内存 + SampleClipIntoPose 遍历越界
        // clip → UB）。归一化为首样本坐标（语义 = 参数无效 → 取第一个样本）。
        if (!(x == x)) // x != x 当且仅当 x 为 NaN
        {
            x = samples[0].position;
        }

        // x ≤ 首样本 或 只有 1 个样本 → 直接采样首样本（clamp，无外插）。
        if (x <= samples[0].position || n == 1)
        {
            out = baseline;
            SampleClipIntoPose(samples[0].clip, p * samples[0].clip.duration, out);
            return;
        }
        // x ≥ 末样本 → 直接采样末样本。
        if (x >= samples[n - 1].position)
        {
            out = baseline;
            SampleClipIntoPose(samples[n - 1].clip, p * samples[n - 1].clip.duration, out);
            return;
        }

        // 找包围区间 [i, i+1)：samples[i].position ≤ x < samples[i+1].position。
        std::size_t i = 0;
        while (i + 1 < n && !(x < samples[i + 1].position))
        {
            ++i;
        }
        const float span = samples[i + 1].position - samples[i].position;
        const float t01  = (span > 0.0f) ? (x - samples[i].position) / span : 0.0f; // 升序去重 → span>0

        Scene::TransformComponent poseA = baseline;
        SampleClipIntoPose(samples[i].clip, p * samples[i].clip.duration, poseA);
        Scene::TransformComponent poseB = baseline;
        SampleClipIntoPose(samples[i + 1].clip, p * samples[i + 1].clip.duration, poseB);

        const Scene::TransformComponent poses[2] = {poseA, poseB};
        const float                     w[2]     = {1.0f - t01, t01};
        BlendPoses(poses, w, 2, out);
    }

    // 用当前参数 x 对 1D blend space 的样本 clip 时长做同权重插值，得到"混合时长"——
    // BlendSpaceAnimator 用它归一化推进 phase（播放速度随混合 clip 长度平滑变化，避免
    // 从 walk 混到 run 时步频突变）。bracketing 与 EvaluateBlendSpace1D 一致：端点取端点
    // clip duration，区间内按 t01 线性插值 durationA/durationB。n==0 → 0。
    inline float BlendedClipDuration1D(const BlendSpace1D& space, float x)
    {
        const std::vector<BlendSample1D>& samples = space.samples;
        const std::size_t                 n       = samples.size();
        if (n == 0)
        {
            return 0.0f;
        }
        // NaN 防护（同 EvaluateBlendSpace1D）：否则 bracketing 循环越界读 samples[i+1]。
        if (!(x == x))
        {
            x = samples[0].position;
        }
        if (x <= samples[0].position || n == 1)
        {
            return samples[0].clip.duration;
        }
        if (x >= samples[n - 1].position)
        {
            return samples[n - 1].clip.duration;
        }
        std::size_t i = 0;
        while (i + 1 < n && !(x < samples[i + 1].position))
        {
            ++i;
        }
        const float span = samples[i + 1].position - samples[i].position;
        const float t01  = (span > 0.0f) ? (x - samples[i].position) / span : 0.0f;
        return glm::mix(samples[i].clip.duration, samples[i + 1].clip.duration, t01);
    }

    // ---- 2D blend space（双参数，如 forward×strafe / speed×turn）-----------------

    // 一个 2D 样本：在参数平面 position 处对应一个 clip。
    struct BlendSample2D
    {
        glm::vec2     position{0.0f, 0.0f};
        AnimationClip clip;
    };

    struct BlendSpace2D
    {
        std::vector<BlendSample2D> samples;
    };

    // 在参数平面点 p 处对 2D blend space 求值（反距离权重 IDW，power 2）。
    //   * 精确命中某样本点（dist < eps）→ 直接采样该 clip（避免除零 + 命中样本应精确）。
    //   * 否则各样本权重 w_i = 1 / dist_i²，采样进 baseline 副本后 BlendPoses 归一化混合。
    // 注：IDW 是 v1 简化——命中样本精确、样本密集处平滑，但等距多点会取算术平均（见测试）。
    // Delaunay / gradient-band 精确重心插值（Unreal AnimationBlendSpace 用）留 follow-up。
    inline void EvaluateBlendSpace2D(const BlendSpace2D& space, glm::vec2 p, float phase,
                                     const Scene::TransformComponent& baseline,
                                     Scene::TransformComponent&       out)
    {
        const std::vector<BlendSample2D>& samples = space.samples;
        const std::size_t                 n       = samples.size();
        const float                       ph      = glm::clamp(phase, 0.0f, 1.0f);

        if (n == 0)
        {
            out = baseline;
            return;
        }
        if (n == 1)
        {
            out = baseline;
            SampleClipIntoPose(samples[0].clip, ph * samples[0].clip.duration, out);
            return;
        }

        // NaN 防护：p 含 NaN → dist 全 NaN、IDW 权重 NaN → 输出 NaN 污染 Transform。
        // 归一化为首样本（参数无效 → 取第一个样本）。
        if (!(p.x == p.x) || !(p.y == p.y))
        {
            out = baseline;
            SampleClipIntoPose(samples[0].clip, ph * samples[0].clip.duration, out);
            return;
        }

        // 先算各样本距离；精确命中（dist<eps）→ 直接采样该 clip（避免 IDW 除零）。
        std::vector<float> dist(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            dist[i] = glm::length(p - samples[i].position);
            if (dist[i] < 1e-5f)
            {
                out = baseline;
                SampleClipIntoPose(samples[i].clip, ph * samples[i].clip.duration, out);
                return;
            }
        }

        // IDW power 2：w_i = 1/dist_i²。BlendPoses 内部归一化，故此处不必先归一化。
        std::vector<Scene::TransformComponent> poses(n);
        std::vector<float>                     weights(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            Scene::TransformComponent pose = baseline;
            SampleClipIntoPose(samples[i].clip, ph * samples[i].clip.duration, pose);
            poses[i]   = pose;
            weights[i] = 1.0f / (dist[i] * dist[i]);
        }
        BlendPoses(poses.data(), weights.data(), n, out);
    }

} // namespace Orange::Engine::Animation

#endif // ORANGE_ENGINE_ANIMATION_BLEND_SPACE_H
