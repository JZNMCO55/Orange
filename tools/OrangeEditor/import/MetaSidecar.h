#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_META_SIDECAR_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_META_SIDECAR_H

// ---------------------------------------------------------------------------
// MetaSidecar —— 外部 DCC 资产 import 后同目录写盘的 `.meta` 描述文件。
//
// 工业标配（Unity / Unreal / Lumix / Cocos / Godot）：每个 import 后的资产
// 同目录维护一个 sidecar 文件，记 source hash + import 参数 + 引擎内部
// handle 标识，下次再 import 时按 hash 比对决定是否增量重 import。本模块
// 是 v1.1 T1 阶段的 baseline：先把 schema + JSON I/O + hash util 落下来；
// 真正的 "hash 比对 + 自动 reimport" 接通在 T5 完成。
//
// v1 schema（namespace: editor/import/texture）：
//   {
//     "schemaVersion": {"namespace":"editor/import/texture","major":1,"minor":0},
//     "sourcePath":    "C:/Users/.../foo.png",
//     "sourceHash":    "0xa1b2c3d4e5f60718",
//     "handleId":      0,
//     "importParams":  {}
//   }
//
// 字段语义：
//   - sourcePath：导入时记录的源文件绝对路径（用户感知用，不强制 reimport
//                 走原路径——T5 时按需扩 search policy）
//   - sourceHash：FNV-1a 64-bit 走文件全字节内容；hex 字符串落盘避免
//                 JSON number 精度问题
//   - handleId：占位字段，预留给未来"资产稳定 UUID"扩展；v1 写 0 即可，
//               AssetRegistry::Insert 已经 dedup by path 提供等价稳定性
//   - importParams：v1 空 object 占位；v1.2+ 加 normalmap green invert /
//                   scale / mipmap mode 等字段时不改 schema major（minor
//                   bump + reader 容忍缺失字段）
//
// schema_version 演进按 CLAUDE.md "Serialization and reflection" 节纪律：
// 已发版本不改字段语义，新字段走 minor bump + reader fallback；major bump
// 必须配 migrator。第一版固定 major=1 minor=0。
//
// 归属：editor-time 专属，引擎 runtime 不读 .meta（runtime 通过
// AssetRegistry::Load 直接消费转出来的引擎自家二进制）。本模块属 v1.1
// ADR-008 强制 invariant "新增资产格式必须走 4 件套路径"中的第 (c) 件。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Editor::Import
{

    // .meta v1 实际承载的数据。当前只有 Texture 一种 import 走 .meta（T3/T4
    // 接入后 Mesh 也走同样模板）；为了不在 T1 阶段提前抽象，先保留 Texture
    // 专用结构，T3 时按需引入 `MeshMetaV1` 同款结构 + 共享 helper（hash /
    // schemaVersion 等）。
    struct TextureImportParams
    {
        // v1 占位。v1.2+ 在此扩展：
        //   - 是否 sRGB（albedo 默认 true，normal/roughness 默认 false）
        //   - normalmap green invert（DirectX vs OpenGL 法线坐标系）
        //   - import-time 缩放（2K → 1K 节省 VRAM）
        //   - mipmap mode（运行时生 vs import 时预生）
        // 字段加进来时 schema minor bump + reader 给缺失字段填默认。
    };

    struct TextureMetaV1
    {
        std::string         sourcePath;
        std::uint64_t       sourceHash{0};
        std::uint64_t       handleId{0};
        TextureImportParams importParams;
        // 多 material mesh 导入产物：按 material slot 顺序排列的 .material 落盘
        // 路径（slot i 用第 i 项；某 slot 的 primitive 无 material 时该项为空）。
        // 仅多 material gltf 导入会填它；drop .mesh 到 entity 时回读它挂
        // SubMeshMaterialsComponent。单 material / texture / obj 导入留空——
        // schema minor 维持 0、.meta 字节与历史一致；非空时 minor bump 到 1 并
        // 写出 "subMeshMaterials" 段（已发版本字段语义不变，新字段走 minor bump
        // + reader 容忍缺失，符合 CLAUDE.md serialization 纪律）。
        std::vector<std::string> subMeshMaterials;
    };

    // 计算文件 FNV-1a 64-bit hash。文件不存在 / 不可读 → nullopt。
    // FNV-1a 选型理由：30 行内自实现零 vendor，性能（~500MB/s 单线程）
    // 对 import 流程足够（资产 hash 不是 hot path）；未来撞上性能瓶颈
    // 切 xxh3 不破坏 .meta schema（同样 64-bit hex string 落盘）。
    std::optional<std::uint64_t> ComputeFileHashFnv1a(std::string_view path);

    // 64-bit hash → "0x" 前缀的 16 位小写 hex 字符串。
    std::string HashToHexString(std::uint64_t hash);

    // "0x..." 16 位 hex → 64-bit hash。格式错误 → nullopt。
    std::optional<std::uint64_t> HexStringToHash(std::string_view hex);

    // 读 .meta 文件。文件不存在 / JSON 解析失败 / schemaVersion 不属于
    // editor/import/texture namespace → nullopt + ORANGE_LOG_*。v1.0
    // 文件正常返回；v1.x 高 minor 也接受（前向兼容）。
    std::optional<TextureMetaV1> ReadTextureMeta(std::string_view path);

    // 写 .meta 文件。永远以 v1.0 schema 写盘。失败 → ORANGE_LOG_ERROR
    // + 返回 false。
    bool WriteTextureMeta(std::string_view path, const TextureMetaV1& meta);

    // 资产路径 → .meta sidecar 路径约定：同目录、同 basename、追加 ".meta"
    // 后缀。例：`assets/Textures/foo.png` → `assets/Textures/foo.png.meta`。
    // 与 Lumix / Godot / Unity 行业惯例对齐。
    std::string MetaPathFor(std::string_view assetPath);

    // "目标 asset 路径已有 .meta 且 sourceHash 与 newHash 匹配" 的便利查询。
    // T5 增量重 import：importer 计算源文件 hash 后，调本 helper 比对目标 .meta
    // 里记录的旧 hash —— 一致即跳过 copy / Save / Load 全套，直接复用现有
    // 资产（log INFO "unchanged，skipping reimport"）。
    // 不存在 .meta / hash 不一致 / 解析失败 → 返回 false（走完整 import 路径）。
    bool MetaSourceHashMatches(std::string_view destAssetPath, std::uint64_t newHash);

} // namespace Orange::Editor::Import

#endif // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_META_SIDECAR_H
