#ifndef ORANGE_ENGINE_SAVE_SLOT_MANAGER_H
#define ORANGE_ENGINE_SAVE_SLOT_MANAGER_H

// ---------------------------------------------------------------------------
// SlotManager —— 多存档槽位管理 + sidecar 元数据。
//
// 在 `SaveGameSystem`（Save / Load 单文件）与 `SavePath`（平台路径解析）
// 之上，提供：
//   * "在配置好的 SavePathOptions 目录下，按 slot 名管理 N 个存档" 的高
//     层视角；
//   * 每个存档配套的 sidecar 元数据（`<slot>.meta.json`）—— 记录时间戳
//     / 显示名 / game 自定义摘要 / 是否 autosave 等"无需读完 .save 即可
//     展示给玩家"的信息；
//   * `ListSlots()` 一次性枚举所有 slot + 元数据，给 UI"读档面板"直接
//     喂数据；
//   * 删除（`.save` + `.meta.json` 一并清掉）。
//
// 文件布局（位于 `ResolveSaveDirectory(options)` 下）：
//
//     <dir>/slot1.save        ← SaveGameSystem 主文件
//     <dir>/slot1.meta.json   ← 本模块写的 sidecar
//     <dir>/autosave.save     ← autosave 槽（与 slot1 同模式，仅 isAutosave 标记不同）
//     <dir>/autosave.meta.json
//
// 选 sidecar 而不是把元数据塞进 `.save` 内部的理由：
//   * `ListSlots()` 只需要读小 JSON，避免遍历 N 个 GB 级存档；
//   * 元数据格式独立演化，不污染 SaveGameSystem 的 OSAV header；
//   * `.meta.json` 损坏时退化到"用 filesystem mtime 当时间戳 + 显示
//     placeholder"，slot 仍然可读 / 可加载，只是没有摘要 UI。
//
// "自动存档槽" 是约定（typical: slot 名 = "autosave"），引擎不强制；
// `SlotMetadata::isAutosave` 字段是给 game UI 看的标记，不影响存取语义。
//
// SlotManager 自身无状态（持有 SavePathOptions 副本即可），可任意线程
// 并发使用——但**单一 slot 名在并发 Save 下行为未定义**（与
// `SaveGameSystem::Save` 同——原子 rename 在两个并发写场景下哪个赢未指
// 定）。Game 侧若要并发存盘，自己加锁。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/save/SavePath.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine
{

    class World;

} // namespace Orange::Engine

namespace Orange::Engine::Save
{

    class SaveGameSystem;

    // SlotMetadata —— sidecar `<slot>.meta.json` 的字段结构；同时是
    // `SlotManager::Save` 的入参与 `ListSlots` / `ReadMetadata` 的输出。
    //
    // 字段约定：
    //   * `slotName`：槽位名，等于 `.save` 文件的 stem（不含扩展名）。Save
    //     时由 SlotManager 用调用方传入的 slotName 覆写——game 侧在 metadata
    //     里填什么都会被覆盖，避免 metadata 与文件名不一致。
    //   * `displayName` / `summary`：完全由 game 自定义，引擎不解读。空字符
    //     串视为"未设置"，UI 端用 placeholder 展示。
    //   * `savedAtUnixSeconds`：保存时刻的 wall-clock UTC 秒数（自 1970 年起）。
    //     Save 时若入参为 0 则由 SlotManager 用当前系统时间填，否则尊重调
    //     用方提供的值（让 game 端可以注入测试用固定时间）。
    //   * `playTimeSeconds`：game 累计的游戏时长（不是 wall clock）；引擎
    //     不维护，game 自己累加后填进来。
    //   * `engineSaveFormatVersion`：本次保存时 SaveGameSystem 的 header 版
    //     本号，给"可读 / 不可读"的快速预筛用（不替代真正的 schema 校验，
    //     真正的兼容性判定仍由 SaveGameSystem::Load 内部走）。Save 时由
    //     SlotManager 内部覆写为当前版本，game 不必填。
    //   * `isAutosave`：纯 UI 标记，区分 autosave 槽与手动槽；引擎不强制。
    struct SlotMetadata
    {
        std::string   slotName{};
        std::string   displayName{};
        std::string   summary{};
        std::int64_t  savedAtUnixSeconds{0};
        std::int64_t  playTimeSeconds{0};
        std::uint32_t engineSaveFormatVersion{0};
        bool          isAutosave{false};
    };

    class ORANGE_ENGINE_API SlotManager
    {
    public:
        // options.game 必须非空 / 路径片段合法（具体校验沿用 SavePath 的
        // 同名规则）—— 校验失败延迟到第一次实际调用时返回错误，构造本身
        // 不 throw，让"游戏启动期早早创建 SlotManager"这个常见 pattern 不
        // 受限。
        explicit SlotManager(SavePathOptions options) noexcept;

        SlotManager(const SlotManager&)                = default;
        SlotManager& operator=(const SlotManager&)     = default;
        SlotManager(SlotManager&&) noexcept            = default;
        SlotManager& operator=(SlotManager&&) noexcept = default;

        // 综合操作：调 `system.Save(world, <slot>.save)`，成功后再原子写出
        // `<slot>.meta.json`。
        //
        // metadata 处理：
        //   * `slotName` 字段被 SlotManager 用入参 slotName 覆写
        //   * `engineSaveFormatVersion` 被覆写为当前 SaveGameSystem 版本
        //   * `savedAtUnixSeconds == 0` 时由 SlotManager 用 std::time(nullptr) 填
        //   * 其它字段保持调用方传入值
        //
        // 失败语义：
        //   * slotName 为空 / 含非法字符                  → InvalidArgument
        //   * SavePath 解析失败（vendor / game 不合法等） → 透传 SavePath 的错误码
        //   * `system.Save` 失败                          → 透传该错误码（metadata
        //                                                   不会被写）
        //   * `system.Save` 成功但 metadata sidecar 写入失败 → IoError
        //                                                  （**`.save` 已落地**，slot
        //                                                   仍可加载；仅元数据缺失，
        //                                                   ListSlots 时退化到 placeholder）
        Result<void, ResultCode> Save(const SaveGameSystem& system,
                                      const World&          world,
                                      std::string_view      slotName,
                                      SlotMetadata          metadata) const;

        // 仅读 sidecar 元数据；不动 `.save` 文件（loading 由调用方自己用
        // SaveGameSystem.Load(slot.savePath, world) 触发）。
        //
        // 失败语义：
        //   * slotName 不合法                       → InvalidArgument
        //   * `<slot>.meta.json` 不存在 / 不可读     → IoError
        //   * JSON 解析失败 / schemaVersion 不兼容   → InvalidArgument / SchemaMismatch
        Result<SlotMetadata, ResultCode> ReadMetadata(std::string_view slotName) const;

        // 枚举存档目录下所有 `*.save` 文件，返回每个 slot 的元数据快照。
        //
        // 行为：
        //   * 仅 `.save` 存在判定为有效 slot；orphan 的 `.meta.json`（无
        //     对应 `.save`）跳过；
        //   * `.meta.json` 缺失 / 损坏时回退到 "slotName + 文件 mtime + 其
        //     它字段 default" 的 placeholder（warn 一次，不视为整体失败）；
        //   * 残留的 `<slot>.save.tmp`（被中断的写入）跳过；
        //   * 返回顺序按 `savedAtUnixSeconds` 降序（最近的存档排前），
        //     placeholder 时按 mtime 降序排在一起。
        //
        // 失败语义：
        //   * SavePath 解析失败                  → 透传错误码
        //   * 目录尚未存在（首次启动）            → 返回空 vector（**不**视为错误）
        //   * directory_iterator 失败             → IoError
        Result<std::vector<SlotMetadata>, ResultCode> ListSlots() const;

        // 删除 `<slot>.save` + `<slot>.meta.json`（best effort each）。
        //
        // 失败语义：
        //   * slotName 不合法                  → InvalidArgument
        //   * `.save` 删除失败                  → IoError（`.meta.json` 仍尝试删除）
        //   * `.save` 不存在                    → 视为无操作 + Ok（幂等）
        Result<void, ResultCode> DeleteSlot(std::string_view slotName) const;

        // `<slot>.save` 是否存在。slotName 不合法返回 false（不抛错，便于
        // UI 端无脑 query）。
        bool SlotExists(std::string_view slotName) const noexcept;

        // 路径解析门面 —— Save / Load 内部都用得到，公开出来让 game 端在
        // 需要绕过 SlotManager 自己读写 sidecar 时也能拿到稳定路径。
        Result<std::filesystem::path, ResultCode>
        ResolveSavePath(std::string_view slotName) const;

        Result<std::filesystem::path, ResultCode>
        ResolveMetadataPath(std::string_view slotName) const;

        const SavePathOptions& Options() const noexcept { return mOptions; }

    private:
        SavePathOptions mOptions;
    };

} // namespace Orange::Engine::Save

#endif // ORANGE_ENGINE_SAVE_SLOT_MANAGER_H
