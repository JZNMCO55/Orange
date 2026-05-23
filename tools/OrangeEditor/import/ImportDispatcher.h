#ifndef ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_DISPATCHER_H
#define ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_DISPATCHER_H

// ---------------------------------------------------------------------------
// ImportDispatcher —— 外部 DCC 资产 import 的统一入口。按文件扩展名分派
// 到具体 importer 模块（Texture / Obj mesh / Gltf mesh），执行 ADR-008
// 强制 4 件套路径中的 (b) (c) (d) 三件：
//
//   (b) 转引擎自家二进制 + copy 到 `assets/<TypeDir>/`
//   (c) 同目录写 `.meta` JSON
//   (d) AssetRegistry runtime `Insert<T>` / `Load<T>`
//
// 第 (a) 件"vendor 接 + 单 header 优先"是 T3/T4 各自 importer 的引入点。
//
// 入口模式（ADR-008 议题 A3 决策）：
//   * File → Import... 菜单（EditorRenderLayer 内调）
//   * OS 文件 drag-drop（main.cpp glfwSetDropCallback → EditorHost.pendingImports
//     队列，OnUpdate 帧末 drain）
//   * 两路都不弹对话框，参数走 .meta 默认值；高级参数后续 v1.2+ 在
//     Inspector 段加可写 GUI（参 editor-roadmap.md L17 / v1.x 长尾）
// ---------------------------------------------------------------------------

#include <string>
#include <string_view>

struct EditorHost;

namespace Orange::Editor::Import
{

// 按扩展名分类。Dispatch 内部用；外部 caller 一般直接调 Dispatch。
enum class ImportKind
{
    Texture,      // .png / .jpg / .jpeg / .tga / .hdr
    ObjMesh,      // .obj
    GltfMesh,     // .gltf / .glb
    Unsupported,  // 其它
};

// 执行结果状态。Success 表示 import + copy + .meta + AssetRegistry 全部 OK。
enum class ImportStatus
{
    Success,
    UnsupportedExt,        // 扩展名不在支持清单
    SourceReadFailed,      // 源文件不存在 / 不可读 / hash 计算失败
    CopyFailed,            // copy 到 assets/<TypeDir>/ 失败（权限 / 磁盘空间）
    AssetLoadFailed,       // copy OK 但 AssetRegistry::Load 后处理失败
    MetaWriteFailed,       // 资产 ready 但 .meta 写盘失败
    NotImplemented,        // mesh 路径在 T3/T4 接通前的 stub 返回值
};

struct ImportResult
{
    ImportStatus status{ImportStatus::Success};
    std::string  destPath;      // assets/<TypeDir>/<filename>，成功时填
    std::string  message;       // 人类可读 + 用于 log
};

// 按小写扩展名分类。"png" / "obj" 不带 '.'；'.png' / 'png' 都接受。
ImportKind ClassifyByExt(std::string_view ext);

// 单点入口：按 ext 路由到具体 importer。
//
// srcPath: OS 文件绝对或相对路径（drag-drop 或 dialog 选出来的）；不能为空。
// host:    用 host.assets.pAssets 拿 AssetRegistry 注入资产；host 必须有
//          有效的 pAssets（否则返 AssetLoadFailed）。
ImportResult Dispatch(std::string_view srcPath, EditorHost& host);

// Texture importer：T1 落地（PNG/JPG/JPEG/TGA/HDR）。
// dest = assets/Textures/<basename>；overwrite 已存在文件（reimport 语义）。
ImportResult ImportTexture(std::string_view srcPath, EditorHost& host);

// Obj mesh importer：T3 接通。T2 阶段 stub 返回 NotImplemented。
ImportResult ImportObjMesh(std::string_view srcPath, EditorHost& host);

// Gltf mesh importer：T4 接通。T2 阶段 stub 返回 NotImplemented。
ImportResult ImportGltfMesh(std::string_view srcPath, EditorHost& host);

}  // namespace Orange::Editor::Import

#endif  // ORANGE_ENGINE_TOOLS_EDITOR_IMPORT_DISPATCHER_H
