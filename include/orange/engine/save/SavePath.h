#ifndef ORANGE_ENGINE_SAVE_SAVE_PATH_H
#define ORANGE_ENGINE_SAVE_SAVE_PATH_H

// ---------------------------------------------------------------------------
// SavePath —— 平台相关的"玩家进度存档放在哪里"路径解析。
//
// SaveGameSystem 自身只接受最终的绝对路径字符串；本 helper 负责把
// "Windows 上 %APPDATA% / Linux 上 $XDG_DATA_HOME" 这一层平台细节封住，
// 让 game 侧只需要描述 vendor / game / subfolder 三件套，不必直接碰
// `Shlobj.h` / 环境变量。
//
// 当前覆盖：
//   * Windows：`SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE)`
//   * 其它平台：返回 `ResultCode::Unsupported`，接口预留——Linux 通常是
//     `$XDG_DATA_HOME` / `~/.local/share`，macOS 通常是
//     `~/Library/Application Support`，将来按需补。
//
// 典型路径形态（Windows）：
//   `<APPDATA>/<vendor>/<game>/<subfolder>/<slotName>.save`
// 例：
//   `C:\Users\<user>\AppData\Roaming\OrangeEngine\OriClone\saves\slot1.save`
//
// ResolveSaveDirectory / ResolveSaveSlotPath 调用时**会创建目录**（mkdir
// -p 行为），让上层 `SaveGameSystem::Save` 可以直接把返回路径作为目标
// 写入；调用方不需要再 `create_directories`。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Orange::Engine::Save
{

    // SavePathOptions —— 描述"我这一份存档落在 user-data 根下哪个子目录"。
    //
    // 字段约束：
    //   * vendor   ：默认 "OrangeEngine"。建议 game 侧改成自己的工作室 / 发
    //                行商名字，避免不同 game 混在同一个 OrangeEngine 子目录
    //                下相互踩。
    //   * game     ：**必填，不能为空**。同一台机器上多个 OrangeEngine 游戏
    //                共用 vendor 文件夹时靠这个字段隔离。
    //   * subfolder：默认 "saves"。截图 / 配置 / log 等子系统未来若要复用
    //                同一 user-data 根，按这一字段分流（例 "screenshots" /
    //                "configs"）。
    //
    // 三个字段都被当作"目录名片段"使用——含路径分隔符 / 跨目录的字符
    // （`/` `\` `..`）会被 reject 为 InvalidArgument，避免越权写入用户其它
    // 目录。
    struct SavePathOptions
    {
        std::string vendor{"OrangeEngine"};
        std::string game{};
        std::string subfolder{"saves"};
    };

    // 返回当前用户的"漫游应用数据"根目录。
    //
    //   * Windows：`SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE)`
    //     —— 典型形如 `C:\Users\<user>\AppData\Roaming`。`KF_FLAG_CREATE`
    //     让 OS 在该目录不存在时帮我们建好（罕见，主要为新装系统兜底）。
    //   * 其它平台：返回 `ResultCode::Unsupported`，等接平台层时再补。
    ORANGE_ENGINE_API Result<std::filesystem::path, ResultCode> ResolveUserDataRoot();

    // 在 user-data 根下拼出 `<vendor>/<game>/<subfolder>/` 并 mkdir -p。返回
    // 已确保存在的绝对目录路径。
    //
    // 失败语义：
    //   * `options.game` 为空                              → InvalidArgument
    //   * `vendor` / `game` / `subfolder` 含路径分隔符      → InvalidArgument
    //   * `ResolveUserDataRoot()` 失败（非 Windows 等）    → 透传该错误码
    //   * `std::filesystem::create_directories` 失败       → IoError
    ORANGE_ENGINE_API Result<std::filesystem::path, ResultCode>
                      ResolveSaveDirectory(const SavePathOptions& options);

    // `ResolveSaveDirectory(options)` + 追加 `<slotName>.save`。返回的是文件
    // 路径（**不**预创建文件，仅保证父目录存在）。
    //
    // 失败语义：
    //   * `slotName` 为空 / 含路径分隔符  → InvalidArgument
    //   * `ResolveSaveDirectory` 失败     → 透传错误码
    ORANGE_ENGINE_API Result<std::filesystem::path, ResultCode>
                      ResolveSaveSlotPath(const SavePathOptions& options, std::string_view slotName);

} // namespace Orange::Engine::Save

#endif // ORANGE_ENGINE_SAVE_SAVE_PATH_H
