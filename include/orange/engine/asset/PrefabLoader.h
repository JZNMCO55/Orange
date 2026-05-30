#ifndef ORANGE_ENGINE_ASSET_PREFAB_LOADER_H
#define ORANGE_ENGINE_ASSET_PREFAB_LOADER_H

// ---------------------------------------------------------------------------
// PrefabLoader —— .prefab.json 资源的同步加载 / 保存器。
//
// 磁盘格式（独立 schema namespace "prefab/asset" 1.0，形态 B）：
//   {
//     "schemaVersion": { "namespace": "prefab/asset", "major": 1, "minor": 0 },
//     "prefabName": "...",
//     "template": "<Scene::SaveSubtreeToString 产出的 scene/world JSON 原文，
//                   作为一个 JSON 字符串字段>"
//   }
//
// 形态 B 把 template 存为字符串字段（而非内联 JSON 对象），换来两点：
//   * 加载零二次解析：PrefabAsset 直接持有这段原文，实例化时整段喂给
//     Scene::LoadFromString。
//   * 字节保真：Save→Load 往返不改 template 一个字节，与 SaveSubtreeToString
//     的输出严格一致。
//
// JSON 全走 Core::Serialization（JsonReader / JsonWriter），不直接碰
// nlohmann（header isolation + 序列化纪律）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/asset/IAssetLoader.h>
#include <orange/engine/asset/PrefabAsset.h>
#include <orange/engine/core/Result.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace Orange::Engine::Asset
{

class ORANGE_ENGINE_API PrefabLoader final : public IAssetLoader<PrefabAsset>
{
public:
    // schema 常量。Load 校验 namespace 必须 bit-for-bit 匹配 + major == 1；
    // minor 向后兼容由 SchemaVersion::CanRead 处理。
    static constexpr std::string_view kSchemaNamespace = "prefab/asset";
    static constexpr std::uint16_t     kSchemaMajor     = 1;
    static constexpr std::uint16_t     kSchemaMinor     = 0;

    PrefabLoader()           = default;
    ~PrefabLoader() override = default;

    // 从 path 读 .prefab.json，解出 PrefabAsset。
    // 失败码：NotFound / IoError（文件读不到）、InvalidArgument（JSON 坏 /
    // 必填字段缺失）、SchemaMismatch（namespace / major 不兼容）。
    Result<std::unique_ptr<PrefabAsset>, ResultCode> Load(std::string_view path) override;

    // 把 (name, templateBlob) 写成 prefab/asset 1.0 的 .prefab.json 到 path。
    // caller 保证目标目录已存在；本函数不创建目录。
    // 失败码：IoError（无法写文件）。
    static Result<void, ResultCode> Save(std::string_view path,
                                         std::string_view prefabName,
                                         std::string_view templateBlob);
};

}  // namespace Orange::Engine::Asset

#endif  // ORANGE_ENGINE_ASSET_PREFAB_LOADER_H
