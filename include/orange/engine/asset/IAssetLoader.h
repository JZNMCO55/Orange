#ifndef ORANGE_ENGINE_ASSET_I_ASSET_LOADER_H
#define ORANGE_ENGINE_ASSET_I_ASSET_LOADER_H

// ---------------------------------------------------------------------------
// IAssetLoader<T> —— 单个 asset 类型 T 的同步加载抽象。
//
// 每种内置 / 用户自定义资源（Mesh、Texture、Shader、Skeleton、Sound、
// Font…）通过 implement 一份 `IAssetLoader<T>` 子类把"路径 → 内存对
// 象"的解码过程接入 AssetRegistry。Registry 通过 `RegisterLoader<T>`
// 拿到一个 unique_ptr，并在 Load<T>(path) 时路由到对应 loader。
//
// 设计要点：
//   * 接口刻意保持狭窄——只负责"从路径解出一个 T"，不管缓存、引用
//     计数、异步：这些是 AssetRegistry 的职责。当前只有单一同步路
//     径；async loading 后续再加。
//   * 失败一律走 Result<unique_ptr<T>, ResultCode>；不抛异常、不返回
//     裸 nullptr。常见 ResultCode：NotFound（文件不存在）、IoError
//     （读盘失败）、InvalidArgument（解码失败）、SchemaMismatch（带
//     schema 的资源版本不匹配）。
//   * 返回 unique_ptr 而非 shared_ptr：所有权一进 Registry 就被
//     Registry 接管；外部读取走 Get<T>(handle) 返回 const T*。
//
// 何时不要继承本接口：
//   * 需要异步 / 流式加载 → 后续 Phase 引入独立的 IAssetStreamer。
//   * 资源需要在加载完成后注入到 GPU（如 Texture 上传）→ loader
//     的输出对象自己内部持有上传句柄；loader 接口本身保持 platform-
//     agnostic。
// ---------------------------------------------------------------------------

#include <orange/engine/core/Result.h>

#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

template <typename T>
class IAssetLoader
{
public:
    virtual ~IAssetLoader() = default;

    IAssetLoader()                                     = default;
    IAssetLoader(const IAssetLoader&)                  = delete;
    IAssetLoader& operator=(const IAssetLoader&)       = delete;
    IAssetLoader(IAssetLoader&&) noexcept              = delete;
    IAssetLoader& operator=(IAssetLoader&&) noexcept   = delete;

    virtual Result<std::unique_ptr<T>, ResultCode> Load(std::string_view path) = 0;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_I_ASSET_LOADER_H
