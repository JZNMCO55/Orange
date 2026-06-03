// ClipAnimatorAssetReassignTest —— 锁住编辑器 Inspector "clip 重指派"字段
// （B2.6 改点 3 的 clipSet）所依赖的纯数据序列：
//   AssetRegistry::Load<AnimationClip>(path) → Get → ClipAnimator::SetClip(*loaded)
//   + SetSourceAssetPath(path)
// 之后 ClipAnimator 反映新 clip（duration / track 采样）+ 记住来源路径。
//
// 这段逻辑在编辑器里由 schema FieldAssetRef 的 clipSet lambda 承载（GUI 不可
// headless 测）；本测试把同一序列**不经 GUI**直接跑一遍，验证数据语义。纯
// CPU：AssetRegistry + 临时 .anim 文件 + 栈上 TransformComponent，无 GPU。

#include "orange/engine/animation/ClipAnimator.h"

#include "orange/engine/animation/AnimationClip.h"
#include "orange/engine/animation/AnimationClipLoader.h"
#include "orange/engine/animation/AnimationClipSerialization.h"
#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/scene/TransformComponent.h"

#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace Anim  = ::Orange::Engine::Animation;
namespace Asset = ::Orange::Engine::Asset;
namespace Scene = ::Orange::Engine::Scene;

namespace
{

bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

std::filesystem::path MakeTempAnimPath(const char* tag)
{
    auto base = std::filesystem::temp_directory_path();
    base /= std::string{"orange_engine_clip_reassign_"} + tag + ".anim";
    std::error_code ec;
    std::filesystem::remove(base, ec);
    return base;
}

// 造一个 position.x 0→6 线性、duration=2 的 clip 并落盘。
Anim::AnimationClip MakeClipFile(const std::filesystem::path& path)
{
    Anim::AnimationClip clip;
    clip.name     = "reassign_clip";
    clip.duration = 2.0f;
    clip.loop     = false;
    Anim::AnimationTrack t;
    t.targetName = "position.x";
    t.valueType  = Anim::TrackValueType::Float;
    Anim::Keyframe k0;
    k0.time  = 0.0f;
    k0.value = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    Anim::Keyframe k1;
    k1.time  = 2.0f;
    k1.value = glm::vec4(6.0f, 0.0f, 0.0f, 0.0f);
    t.keys.push_back(k0);
    t.keys.push_back(k1);
    clip.tracks.push_back(t);

    auto saveRes = Anim::SaveAnimationClip(clip, path.string());
    assert(saveRes.IsOk() && "Save .anim 应成功");
    return clip;
}

}  // namespace

int main()
{
    const auto path = MakeTempAnimPath("via_inspector");
    MakeClipFile(path);

    Asset::AssetRegistry reg;
    auto regRes = reg.RegisterLoader<Anim::AnimationClip>(
        std::make_unique<Anim::AnimationClipLoader>());
    assert(regRes.IsOk() && "RegisterLoader 应成功");

    // ===== 1. 空 ClipAnimator（+Add Component "Animator (Clip)" 后的初态）=====
    Scene::TransformComponent target;
    target.position = glm::vec3(0.0f);
    Anim::ClipAnimator animator(Anim::AnimationClip{}, &target);
    assert(animator.SourceAssetPath().empty()
           && "空 clip animator 的来源路径应为空");
    assert(Near(animator.Duration(), 0.0f)
           && "空 clip duration 应为 0（无关键帧）");
    std::fprintf(stdout, "  [PASS] 空 ClipAnimator 初态\n");

    // ===== 2. clipSet 序列：Load → Get → SetClip + SetSourceAssetPath =====
    {
        auto handleRes = reg.Load<Anim::AnimationClip>(path.string());
        assert(handleRes.IsOk() && "Load<AnimationClip> 应成功");
        const Anim::AnimationClip* loaded =
            reg.Get<Anim::AnimationClip>(handleRes.Value());
        assert(loaded != nullptr && "Get 应返回非空 clip");

        animator.SetClip(*loaded);
        animator.SetSourceAssetPath(path.string());
    }

    // 来源路径已记住（Inspector clipGet 读它显示字段值）。
    assert(animator.SourceAssetPath() == path.string()
           && "SetSourceAssetPath 后来源路径应匹配");
    // duration 跟随新 clip。
    assert(Near(animator.Duration(), 2.0f)
           && "重指派后 duration 应为新 clip 的 2.0");
    std::fprintf(stdout, "  [PASS] 重指派后 source path + duration\n");

    // ===== 3. Seek 应用 pose（scrub slider 路径）—— 中点 t=1 → position.x=3 =====
    animator.Seek(1.0f);
    assert(Near(target.position.x, 3.0f)
           && "Seek(1.0) 应把 position.x 采样到 3.0（0→6 线性中点）");
    std::fprintf(stdout, "  [PASS] Seek 应用 pose（scrub 路径）\n");

    // ===== 4. 清空字段（clipSet path 为空分支）：换空 clip + 清来源 =====
    animator.SetClip(Anim::AnimationClip{});
    animator.SetSourceAssetPath({});
    assert(animator.SourceAssetPath().empty()
           && "清空字段后来源路径应为空");
    assert(Near(animator.Duration(), 0.0f)
           && "清空字段后 duration 应回 0");
    std::fprintf(stdout, "  [PASS] 清空字段（空 clip + 清来源）\n");

    std::error_code ec;
    std::filesystem::remove(path, ec);

    std::fprintf(stdout, "ClipAnimatorAssetReassignTest: all passed\n");
    return 0;
}
