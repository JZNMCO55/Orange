#ifndef ORANGE_ENGINE_ASSET_SKELETON_LOADER_H
#define ORANGE_ENGINE_ASSET_SKELETON_LOADER_H

// ---------------------------------------------------------------------------
// SkeletonLoader —— DragonBones .json / .dbbin skeleton 数据的同步加载器。
//
// 每个 SkeletalAnimator 后端实例（DragonBonesContext）需要它把磁盘字节
// 喂进 runtime 的 BaseFactory；loader 持 ctx 引用。一个 ctx 配一个 loader：
//
//     auto& ctx = ...;  // 通常由 AppHost 的 Animation 子系统持有
//     registry.RegisterLoader<SkeletonAsset>(
//         std::make_unique<SkeletonLoader>(ctx));
//     auto h = registry.Load<SkeletonAsset>("assets/bullet_01_ske.json");
//
// 头隔离：本头不暴露任何 dragonBones 类型——前向声明 DragonBonesContext，
// 实现在 src/animation/dragonbones/SkeletonLoader.cpp。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/SkeletonAsset.h>
#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Animation::DragonBonesBackend
{
class DragonBonesContext;
}

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API SkeletonLoader final : public IAssetLoader<SkeletonAsset>
{
public:
    // ctx 必须在 loader 整个生命周期内可用——通常由游戏侧 / AppHost 持
    // 共享的 DragonBonesContext，把引用传进来即可。loader 不持所有权。
    explicit SkeletonLoader(Orange::Engine::Animation::DragonBonesBackend::DragonBonesContext& ctx) noexcept;

    // v0.7 c3：无参 ctor —— loader 内部自管一个 DragonBonesContext
    // unique_ptr。适合"调用方不关心 context 生命周期 / 单 World 单 loader
    // 简单消费"场景（如 OrangeEditor 通过 public API 注册 SkeletonLoader
    // 时无法直接构造 DragonBonesContext，因后者头位于 src/ 不在公共面）。
    // 复杂场景（多 World 共享 context / 自定义 event dispatcher）仍走带
    // ctx 参数 ctor。
    SkeletonLoader();

    ~SkeletonLoader() override;

    // 路径以扩展名识别格式：".json" / ".dbjson" → JSON 文本；".dbbin" →
    // 二进制（当前只保 JSON 路径稳定，binary 暂返
    // SchemaMismatch；待真消费 .dbbin 时再补 BinaryDataParser 路径）。
    Result<std::unique_ptr<SkeletonAsset>, ResultCode> Load(std::string_view path) override;

private:
    Orange::Engine::Animation::DragonBonesBackend::DragonBonesContext*                  mpContext{nullptr};
    // 仅无参 ctor 路径持有；带 ctx 参数 ctor 不动这个字段（保持原"loader
    // 不持所有权"语义）。声明顺序 = 析构顺序：mpOwnedContext 析构发生在
    // ~SkeletonLoader（dtor 显式实现在 .cpp 让 forward declared
    // DragonBonesContext 的 unique_ptr 能编出 deleter）。
    std::unique_ptr<Orange::Engine::Animation::DragonBonesBackend::DragonBonesContext>  mpOwnedContext;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_SKELETON_LOADER_H
