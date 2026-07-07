#ifndef ORANGE_EDITOR_PROJECT_ASSET_PATH_MOUNT_H
#define ORANGE_EDITOR_PROJECT_ASSET_PATH_MOUNT_H

// AssetPathMount —— scene JSON 里资产虚拟路径 scheme 的挂载 / 反挂载（M6 路径虚拟化）。
//
// 编辑器启动 / 打开工程时 chdir 到 projectRoot（cwd == projectRoot），故本方案是纯
// 字符串前缀操作——**不**引入绝对路径解析、**不**改 cwd 模型：
//   * project://X  ⇄  X           X = 项目根（= cwd）相对路径，即当前存储形态。
//   * engine://X   →  <engineResourceRoot>/X   引擎 / 编辑器资源（单向，仅用于资源
//                     加载，不进 scene 资产序列化——此处仅为完整性暴露）。
//   * 无 scheme 的 plain X  →  当作 project://X（向后兼容：旧 1.19 scene 存 plain 相对
//                     路径，resolver 恒等透传，Load 照旧靠 cwd 工作）。
//
// 契约：虚拟路径**只**存在于 scene JSON；内存态（AssetRegistry key / material id）始终
// 是 real（cwd 相对）路径。engine SceneSerialization 在写端 real→virtual、读端 virtual→
// real，本文件提供这两个转换的编辑器实现。
//
// 头隔离：刻意只依赖标准库（<string>），**不**碰任何 editor / Vulkan / ImGui 头——
// 这样 AssetPathMount.cpp 能被单独编进 headless 测试（只链 orange_engine）。

#include <string>
#include <string_view>

namespace Orange::Editor::Project
{

    // virtual → real：把 scene JSON 里存的路径解析回内存用的 real 路径。
    //   * engine://X  → <engineResourceRoot>/X（engineResourceRoot 空 → 退化为 X 相对）；
    //   * project://X → X；
    //   * plain（无 :// scheme）/ 空串 → 原样返回。
    // 未识别的其它 scheme 一律原样透传（保守，不猜测）。
    std::string ResolveVirtualPath(std::string_view stored, std::string_view engineResourceRoot);

    // real → virtual：把内存里的项目相对资产路径包成虚拟路径写进 scene JSON。
    //   * 无 scheme 且非空 → "project://" + real；
    //   * 已带 scheme（含 ://）/ 空串 → 原样返回。
    // 只自动虚拟化项目相对资产路径；scene 资产的保存路径不会产出 engine://。
    std::string VirtualizeProjectPath(std::string_view real);

} // namespace Orange::Editor::Project

#endif // ORANGE_EDITOR_PROJECT_ASSET_PATH_MOUNT_H
