#ifndef ORANGE_ENGINE_ASSET_ASSET_REGISTRY_H
#define ORANGE_ENGINE_ASSET_ASSET_REGISTRY_H

// ---------------------------------------------------------------------------
// AssetRegistry —— 引擎运行时的资源注册表。
//
// 职责：
//   * 按类型 T 收集 `IAssetLoader<T>` 实例，按 typeid(T) 路由；
//   * `Load<T>(path)` 调用对应 loader、把结果落入内部表，返回稳定的
//     `AssetHandle<T>`；同 path 复用同一 handle（dedup 缓存）；
//   * `Get<T>(handle)` 返回只读引用；handle 失效时返回 nullptr；
//   * `Unload<T>(handle)` 显式释放。
//
// 公共面是 template，但 Impl 走 type erasure（typeid + 函数指针），
// 让真正的存储与 dispatch 留在一个 .cpp TU 里。template 方法在 header
// 内 forward 到 4 个非模板私有入口（RegisterLoaderErased /
// LoadErased / GetErased / UnloadErased），这 4 个入口操作的全部是
// opaque 类型——不依赖 Impl 完整定义即可在调用方编译期实例化。
//
// 设计取舍：为什么不用 std::any / std::variant 而用 type_index 路由？
//   * std::any 把每个 asset 装箱一次，destruct 路径走 RTTI；asset 数
//     量一旦上千，开销不可忽略；
//   * std::variant 要求所有 asset 类型在编译期可枚举——和"游戏侧能
//     注册自定义资源类型"这一扩展点直接冲突；
//   * type_index + 自定义 deleter 的 type erasure 既保留了运行时可
//     扩展，又避免给热路径加额外间接层。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/core/Result.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API AssetRegistry
{
public:
    AssetRegistry();
    ~AssetRegistry();

    AssetRegistry(const AssetRegistry&)            = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    AssetRegistry(AssetRegistry&&) noexcept;
    AssetRegistry& operator=(AssetRegistry&&) noexcept;

    // 注册一种 asset type T 的 loader。同一 T 重复注册会替换之前的
    // loader（hot-swap）。`loader == nullptr` 视为 InvalidArgument。
    template <typename T>
    Result<void, ResultCode> RegisterLoader(std::unique_ptr<IAssetLoader<T>> loader)
    {
        if (!loader)
        {
            return ResultCode::InvalidArgument;
        }
        IAssetLoader<T>* released = loader.release();
        return RegisterLoaderErased(
            typeid(T),
            released,
            [](void* p) noexcept { delete static_cast<IAssetLoader<T>*>(p); },
            [](void* loaderRaw, std::string_view inPath, void** outRaw) noexcept -> ResultCode {
                auto* l = static_cast<IAssetLoader<T>*>(loaderRaw);
                auto result = l->Load(inPath);
                if (result.IsErr())
                {
                    *outRaw = nullptr;
                    return result.Error();
                }
                *outRaw = result.Value().release();
                return ResultCode::Ok;
            },
            [](void* p) noexcept { delete static_cast<T*>(p); });
    }

    // 同步加载。语义：
    //   * 同 path 重复 Load<T> 走 dedup，返回同一 handle；
    //   * 没注册过 T 的 loader → 返回 ResultCode::Unsupported；
    //   * loader 自身错误透传出来。
    template <typename T>
    Result<AssetHandle<T>, ResultCode> Load(std::string_view path)
    {
        std::uint64_t handleValue = 0;
        ResultCode rc = LoadErased(typeid(T), path, handleValue);
        if (rc != ResultCode::Ok)
        {
            return rc;
        }
        return AssetHandle<T>{handleValue};
    }

    // 把一个已经在内存里的 T 直接登记到 registry。语义：
    //   * `path` 仅作为身份键参与 dedup —— 不要求文件真存在；
    //   * 已存在 path 时旧 entry 被替换、handle 沿用旧值；
    //   * 不需要事先 RegisterLoader<T>——deleter 由 Insert 自带传入。
    //
    // 适用场景：sample / 工具流水线 / 编辑器需要把"程序式构造"的资
    // 源直接挂到 registry 上，而无需先把它写到磁盘 .orme / .ortx 再
    // Load 一次。
    template <typename T>
    Result<AssetHandle<T>, ResultCode> Insert(std::string_view path, std::unique_ptr<T> asset)
    {
        if (!asset)
        {
            return ResultCode::InvalidArgument;
        }
        T* released = asset.release();
        std::uint64_t handleValue = 0;
        ResultCode rc = InsertErased(
            typeid(T), path, released,
            [](void* p) noexcept { delete static_cast<T*>(p); },
            handleValue);
        if (rc != ResultCode::Ok)
        {
            return rc;
        }
        return AssetHandle<T>{handleValue};
    }

    // 通过 handle 取只读资源。无效 / 已卸载 handle 返回 nullptr，绝
    // 不抛。返回值的生存期由 Registry 保证：直至 Unload 或 Registry
    // 析构。
    template <typename T>
    const T* Get(AssetHandle<T> handle) const noexcept
    {
        if (!handle.IsValid())
        {
            return nullptr;
        }
        return static_cast<const T*>(GetErased(typeid(T), handle.Value()));
    }

    // 显式卸载。返回 false 表示 handle 不在表中。
    template <typename T>
    bool Unload(AssetHandle<T> handle)
    {
        if (!handle.IsValid())
        {
            return false;
        }
        return UnloadErased(typeid(T), handle.Value());
    }

    // 已加载资源总数（跨所有类型）。诊断用，不进热路径。
    std::size_t Size() const noexcept;
    bool        Empty() const noexcept;

private:
    using LoaderDeleter = void (*)(void*) noexcept;
    using AssetDeleter  = void (*)(void*) noexcept;
    using LoadInvoker   = ResultCode (*)(void* loaderRaw,
                                         std::string_view path,
                                         void** outAssetRaw) noexcept;

    Result<void, ResultCode> RegisterLoaderErased(
        const std::type_info& type,
        void* loaderRaw,
        LoaderDeleter loaderDeleter,
        LoadInvoker invoker,
        AssetDeleter assetDeleter);

    ResultCode LoadErased(const std::type_info& type,
                          std::string_view path,
                          std::uint64_t& outHandle);

    ResultCode InsertErased(const std::type_info& type,
                            std::string_view path,
                            void* assetRaw,
                            AssetDeleter assetDeleter,
                            std::uint64_t& outHandle);

    const void* GetErased(const std::type_info& type,
                          std::uint64_t handleValue) const noexcept;

    bool UnloadErased(const std::type_info& type,
                      std::uint64_t handleValue);

    struct Impl;
    std::unique_ptr<Impl> mpImpl;
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_ASSET_REGISTRY_H
