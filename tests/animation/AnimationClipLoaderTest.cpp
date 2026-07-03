// AnimationClipLoader 的 headless 测试（B2.2）：.anim 经 AssetRegistry 加载。
// 锁住：注册 loader → Load<AnimationClip>(path) → Get 拿到 clip 数据；
// 同 path dedup 同 handle；文件不存在 → Err（不崩）。纯 CPU，无 GPU。

#include "orange/engine/animation/AnimationClipLoader.h"

#include "orange/engine/animation/AnimationClip.h"
#include "orange/engine/animation/AnimationClipSerialization.h"
#include "orange/engine/asset/AssetRegistry.h"

#include <glm/vec4.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace Anim  = ::Orange::Engine::Animation;
namespace Asset = ::Orange::Engine::Asset;
using ::Orange::Engine::ResultCode;

namespace
{

    bool Near(float a, float b, float eps = 1e-5f)
    {
        return std::fabs(a - b) < eps;
    }

    std::filesystem::path MakeTempAnimPath(const char* tag)
    {
        auto base = std::filesystem::temp_directory_path();
        base /= std::string{"orange_engine_clip_loader_"} + tag + ".anim";
        std::error_code ec;
        std::filesystem::remove(base, ec);
        return base;
    }

} // namespace

int main()
{
    // 造一个 .anim 文件落盘（复用序列化）。
    const auto path = MakeTempAnimPath("via_registry");
    {
        Anim::AnimationClip clip;
        clip.name     = "loaded_clip";
        clip.duration = 1.5f;
        clip.loop     = true;
        Anim::AnimationTrack t;
        t.targetName = "position.x";
        t.valueType  = Anim::TrackValueType::Float;
        Anim::Keyframe k0;
        k0.time  = 0.0f;
        k0.value = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        Anim::Keyframe k1;
        k1.time  = 1.5f;
        k1.value = glm::vec4(9.0f, 0.0f, 0.0f, 0.0f);
        t.keys.push_back(k0);
        t.keys.push_back(k1);
        clip.tracks.push_back(t);

        auto saveRes = Anim::SaveAnimationClip(clip, path.string());
        assert(saveRes.IsOk() && "Save .anim 应成功");
    }

    // ===== 1. 注册 loader → Load → Get 取回 clip =====
    {
        Asset::AssetRegistry reg;
        auto                 regRes = reg.RegisterLoader<Anim::AnimationClip>(
            std::make_unique<Anim::AnimationClipLoader>());
        assert(regRes.IsOk() && "RegisterLoader 应成功");

        auto handleRes = reg.Load<Anim::AnimationClip>(path.string());
        assert(handleRes.IsOk() && "Load<AnimationClip> 应成功");
        auto handle = handleRes.Value();

        const Anim::AnimationClip* clip = reg.Get<Anim::AnimationClip>(handle);
        assert(clip != nullptr && "Get 应返回非空 clip");
        assert(clip->name == "loaded_clip");
        assert(Near(clip->duration, 1.5f));
        assert(clip->loop == true);
        assert(clip->tracks.size() == 1 && clip->tracks[0].targetName == "position.x");
        assert(Near(clip->tracks[0].keys[1].value.x, 9.0f));
        std::fprintf(stdout, "  [PASS] 注册 loader → Load → Get 取回 clip\n");

        // ===== 2. 同 path 复用同 handle（dedup 缓存）=====
        auto handleRes2 = reg.Load<Anim::AnimationClip>(path.string());
        assert(handleRes2.IsOk());
        assert(handleRes2.Value() == handle && "同 path 应复用同 handle");
        std::fprintf(stdout, "  [PASS] 同 path dedup 同 handle\n");
    }

    // ===== 3. 文件不存在 → Err（不崩）=====
    {
        Asset::AssetRegistry reg;
        auto                 regRes = reg.RegisterLoader<Anim::AnimationClip>(
            std::make_unique<Anim::AnimationClipLoader>());
        assert(regRes.IsOk());
        auto handleRes = reg.Load<Anim::AnimationClip>("does_not_exist_xyzzy.anim");
        assert(handleRes.IsErr() && "不存在的 .anim 应返回 Err");
        std::fprintf(stdout, "  [PASS] 文件不存在 → Err\n");
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);

    std::fprintf(stdout, "AnimationClipLoaderTest: all passed\n");
    return 0;
}
