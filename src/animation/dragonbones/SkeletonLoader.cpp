#include "orange/engine/asset/SkeletonLoader.h"

#include "DragonBonesContext.h"

#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#include <dragonBones/DragonBonesHeaders.h>
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Orange::Engine::Asset
{

namespace DBB = Orange::Engine::Animation::DragonBonesBackend;

namespace
{

// 推导文件扩展名（小写）。空 / 无扩展名 → 空 string。仅做精简；不
// 处理多段 .tar.gz 这类边角。
std::string LowerExtension(std::string_view path)
{
    auto pos = path.find_last_of('.');
    if (pos == std::string_view::npos)
    {
        return {};
    }
    std::string ext{path.substr(pos)};
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// 同步把整个文件读入 std::vector<char>。失败返回 std::nullopt。
// JSON 文件的最末尾要保证 null-terminator——DragonBones 的
// JSONDataParser 期望 const char* 是 c-style 字符串。
std::optional<std::vector<char>> ReadEntireFile(std::string_view path, bool addNullTerminator)
{
    // std::ifstream 接受 c-string；string_view 不保证 null-terminated。
    std::string p{path};
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
    {
        return std::nullopt;
    }
    const std::streamsize sz = f.tellg();
    if (sz < 0)
    {
        return std::nullopt;
    }
    f.seekg(0, std::ios::beg);
    std::vector<char> buf(static_cast<std::size_t>(sz) + (addNullTerminator ? 1u : 0u));
    if (sz > 0)
    {
        f.read(buf.data(), sz);
        if (!f)
        {
            return std::nullopt;
        }
    }
    if (addNullTerminator)
    {
        buf[static_cast<std::size_t>(sz)] = '\0';
    }
    return buf;
}

// 从 DragonBonesData 内部 armature[] 收集 ArmatureMeta 列表——bone 名
// 顺序与 runtime 内 ArmatureData::sortedBones / Armature::_bones 对齐
// （见 ArmatureData::cacheFrames + Armature::init 的 bone 初始化路径）。
std::vector<ArmatureMeta> CollectArmatureMeta(const dragonBones::DragonBonesData* data)
{
    std::vector<ArmatureMeta> out;
    if (data == nullptr)
    {
        return out;
    }
    for (const auto& kv : data->armatures)
    {
        const dragonBones::ArmatureData* arm = kv.second;
        if (arm == nullptr)
        {
            continue;
        }
        ArmatureMeta meta;
        meta.name = arm->name;

        // bone 名按 sortedBones 顺序——跟 runtime 内部遍历顺序一致；
        // 没排序过的 ArmatureData 退化用 bones map（按名字字典序）。
        if (!arm->sortedBones.empty())
        {
            meta.boneNames.reserve(arm->sortedBones.size());
            for (const auto* b : arm->sortedBones)
            {
                meta.boneNames.emplace_back(b != nullptr ? b->name : std::string{});
            }
        }
        else
        {
            meta.boneNames.reserve(arm->bones.size());
            for (const auto& bkv : arm->bones)
            {
                meta.boneNames.emplace_back(bkv.first);
            }
        }

        meta.animationNames.reserve(arm->animationNames.size());
        for (const auto& aname : arm->animationNames)
        {
            meta.animationNames.emplace_back(aname);
        }

        out.emplace_back(std::move(meta));
    }
    return out;
}

}  // namespace

SkeletonLoader::SkeletonLoader(DBB::DragonBonesContext& ctx) noexcept
    : mpContext(&ctx)
{
}

SkeletonLoader::~SkeletonLoader() = default;

Result<std::unique_ptr<SkeletonAsset>, ResultCode> SkeletonLoader::Load(std::string_view path)
{
    if (mpContext == nullptr)
    {
        return ResultCode::Unsupported;
    }

    const std::string ext = LowerExtension(path);
    bool        binary    = false;
    if (ext == ".json" || ext == ".dbjson")
    {
        binary = false;
    }
    else if (ext == ".dbbin")
    {
        binary = true;
    }
    else
    {
        // 未识别的扩展名——保守拒绝，避免把任意字节喂进 JSON parser
        // 触发 runtime 内部断言。
        return ResultCode::InvalidArgument;
    }

    auto bytes = ReadEntireFile(path, /*addNullTerminator=*/!binary);
    if (!bytes.has_value())
    {
        return ResultCode::IoError;
    }

    // cacheName 用 path 本身——同 path 两次 Load 时 AssetRegistry 已先
    // dedup，但 parseDragonBonesData 内部仍然走"按名 dedup"的路径，二
    // 次喂同 name 不会重复占内存。
    const std::string cacheName{path};

    dragonBones::DragonBonesData* data =
        mpContext->ParseDragonBonesData(bytes->data(),
                                        bytes->size(),
                                        cacheName,
                                        binary);
    if (data == nullptr)
    {
        // binary 路径本期 stub 返 nullptr（DragonBonesContext 注释有说明）；
        // JSON 解析失败也走这里。统一映射到 SchemaMismatch（"数据存在但
        // 解析失败 / 暂不支持的格式"）。
        return ResultCode::SchemaMismatch;
    }

    auto asset = std::make_unique<SkeletonAsset>(cacheName, CollectArmatureMeta(data));
    return std::unique_ptr<SkeletonAsset>{std::move(asset)};
}

}  // namespace Orange::Engine::Asset
